#!/usr/bin/env python3
"""ScreamRouter"""
import argparse
import datetime
import ipaddress
import os
import signal
import socket
import sys
import threading

import OpenSSL.crypto  # For reading the SSL certificate
import uvicorn
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from fastapi import FastAPI

from screamrouter.screamrouter_logger.screamrouter_logger import \
    get_logger  # Ensure this is here for logger

logger = get_logger(__name__) # Moved logger initialization up

# Attempt to import AudioManager, build if necessary
try:
    from screamrouter_audio_engine import AudioManager
except ImportError:
    logger.warning("Failed to import screamrouter_audio_engine. Attempting to build...")
    import subprocess
    try:
        # Ensure setup.py is executable or use python3 to run it
        build_command = [sys.executable, "setup.py", "build_ext", "--inplace"]
        process = subprocess.Popen(build_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = process.communicate()
        if process.returncode == 0:
            logger.info("Successfully built screamrouter_audio_engine. stdout:\n%s", stdout.decode())
            # Try importing again
            from screamrouter_audio_engine import AudioManager
        else:
            logger.error("Failed to build screamrouter_audio_engine. Error code: %s", process.returncode)
            if stdout:
                logger.error("Build stdout:\n%s", stdout.decode())
            if stderr:
                logger.error("Build stderr:\n%s", stderr.decode())
            sys.exit(1)
    except FileNotFoundError:
        logger.error("setup.py not found. Cannot build screamrouter_audio_engine.")
        sys.exit(1)
    except Exception as e:
        logger.error(f"An unexpected error occurred during build: {e}")
        sys.exit(1)

import screamrouter.constants.constants as constants
from screamrouter.api.api_configuration import APIConfiguration
from screamrouter.api.api_equalizer import APIEqualizer
from screamrouter.api.api_stats import APIStats
from screamrouter.api.api_webrtc import APIWebRTC
from screamrouter.api.api_website import APIWebsite
from screamrouter.api.api_websocket_config import APIWebsocketConfig
from screamrouter.api.api_websocket_debug import APIWebsocketDebug
from screamrouter.api.api_webstream import APIWebStream
from screamrouter.configuration.configuration_manager import ConfigurationManager
from screamrouter.plugin_manager.plugin_manager import PluginManager
from screamrouter.screamrouter_types.configuration import AudioManagerConfig
from screamrouter.utils.mdns_ptr_responder import ManualPTRResponder  # mDNS
from screamrouter.utils.ntp_server import NTPServerProcess  # NTP Server
# from screamrouter.screamrouter_logger.screamrouter_logger import get_logger # Moved up
from screamrouter.utils.utils import set_process_name


def generate_self_signed_certificate(cert_path: str, key_path: str, hostname: str = "screamrouter.local"):
    """
    Generate a self-signed certificate and private key.
    
    Args:
        cert_path: Path where the certificate will be saved
        key_path: Path where the private key will be saved
        hostname: Hostname to use in the certificate (default: screamrouter.local)
    """
    logger.info(f"Generating self-signed certificate for {hostname}...")
    
    # Ensure the directory exists
    cert_dir = os.path.dirname(cert_path)
    if cert_dir and not os.path.exists(cert_dir):
        try:
            os.makedirs(cert_dir, exist_ok=True)
            logger.info(f"Created certificate directory: {cert_dir}")
        except Exception as e:
            logger.error(f"Failed to create certificate directory {cert_dir}: {e}")
            raise
    
    # Generate private key
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
    )
    
    # Get the local hostname if not using the default
    if hostname == "screamrouter.local":
        try:
            hostname = socket.gethostname()
            if not hostname.endswith('.local'):
                hostname = f"{hostname}.local"
        except Exception:
            hostname = "screamrouter.local"
    
    # Create certificate subject and issuer
    subject = issuer = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "US"),
        x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "Auto-Generated"),
        x509.NameAttribute(NameOID.LOCALITY_NAME, "ScreamRouter"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "ScreamRouter"),
        x509.NameAttribute(NameOID.COMMON_NAME, hostname),
    ])
    
    # Build certificate with Subject Alternative Names
    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(private_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.datetime.utcnow())
        .not_valid_after(datetime.datetime.utcnow() + datetime.timedelta(days=3650))  # 10 years
        .add_extension(
            x509.SubjectAlternativeName([
                x509.DNSName(hostname),
                x509.DNSName("screamrouter.local"),
                x509.DNSName("localhost"),
                x509.IPAddress(ipaddress.IPv4Address("127.0.0.1")),
            ]),
            critical=False,
        )
        .sign(private_key, hashes.SHA256())
    )
    
    # Write private key to file
    try:
        with open(key_path, "wb") as key_file:
            key_file.write(
                private_key.private_bytes(
                    encoding=serialization.Encoding.PEM,
                    format=serialization.PrivateFormat.TraditionalOpenSSL,
                    encryption_algorithm=serialization.NoEncryption(),
                )
            )
        # Set appropriate permissions (readable only by owner)
        os.chmod(key_path, 0o600)
        logger.info(f"Private key written to: {key_path}")
    except Exception as e:
        logger.error(f"Failed to write private key to {key_path}: {e}")
        raise
    
    # Write certificate to file
    try:
        with open(cert_path, "wb") as cert_file:
            cert_file.write(cert.public_bytes(serialization.Encoding.PEM))
        logger.info(f"Certificate written to: {cert_path}")
    except Exception as e:
        logger.error(f"Failed to write certificate to {cert_path}: {e}")
        raise
    
    logger.info(f"Self-signed certificate generated successfully for {hostname}")
    logger.info(f"Certificate is valid for 10 years")


def parse_arguments():
    """Parse command-line arguments and set environment variables"""
    parser = argparse.ArgumentParser(
        description='ScreamRouter - Routes PCM audio around for Scream sinks and sources',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    # Add arguments for all user-configurable constants
    parser.add_argument('--scream-receiver-port', type=int, default=16401,
                        help='Port to receive Scream data at')
    parser.add_argument('--scream-per-process-receiver-port', type=int, default=16402,
                        help='Port to receive per-process data at')
    parser.add_argument('--rtp-receiver-port', type=int, default=40000,
                        help='Port to receive RTP data at')
    parser.add_argument('--sink-port', type=int, default=4010,
                        help='Port for a Scream Sink')
    parser.add_argument('--api-port', type=int, default=443,
                        help='Port FastAPI runs on')
    parser.add_argument('--api-host', type=str, default='0.0.0.0',
                        help='Host FastAPI binds to')
    parser.add_argument('--logs-dir', type=str, default='/var/log/screamrouter/logs/',
                        help='Directory logs are stored in')
    parser.add_argument('--console-log-level', type=str, default='INFO',
                        choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
                        help='Log level for stdout')
    parser.add_argument('--log-to-file', type=str, default='True',
                        choices=['True', 'False', 'true', 'false'],
                        help='Determines whether logs are written to files')
    parser.add_argument('--log-entries-to-retain', type=int, default=2,
                        help='Number of previous runs to retain logs for')
    parser.add_argument('--show-ffmpeg-output', type=str, default='False',
                        choices=['True', 'False', 'true', 'false'],
                        help='Show ffmpeg output')
    parser.add_argument('--npm-react-debug-site', type=str, default='False',
                        choices=['True', 'False', 'true', 'false'],
                        help='Enable to use a locally running npm dev server for the React site')
    parser.add_argument('--certificate', type=str, default='/etc/screamrouter/cert/cert.pem',
                        help='SSL certificate path')
    parser.add_argument('--certificate-key', type=str, default='/etc/screamrouter/cert/privkey.pem',
                        help='SSL certificate key path')
    parser.add_argument('--timeshift-duration', type=int, default=300,
                        help='Timeshift duration in seconds')
    parser.add_argument('--configuration-reload-timeout', type=int, default=3,
                        help='Configuration reload timeout in seconds')
    parser.add_argument('--config-path', type=str, default='/etc/screamrouter/config.yaml',
                        help='Path to the configuration file')
    parser.add_argument('--equalizer-config-path', type=str, default='/etc/screamrouter/equalizers.yaml',
                        help='Path to the equalizer configurations file')
    
    args = parser.parse_args()
    
    # Set environment variables from arguments
    # Only set if not already set in environment
    env_mappings = {
        'SCREAM_RECEIVER_PORT': str(args.scream_receiver_port),
        'SCREAM_PER_PROCESS_RECEIVER_PORT': str(args.scream_per_process_receiver_port),
        'RTP_RECEIVER_PORT': str(args.rtp_receiver_port),
        'SINK_PORT': str(args.sink_port),
        'API_PORT': str(args.api_port),
        'API_HOST': args.api_host,
        'LOGS_DIR': args.logs_dir,
        'CONSOLE_LOG_LEVEL': args.console_log_level,
        'LOG_TO_FILE': args.log_to_file,
        'LOG_ENTRIES_TO_RETAIN': str(args.log_entries_to_retain),
        'SHOW_FFMPEG_OUTPUT': args.show_ffmpeg_output,
        'NPM_REACT_DEBUG_SITE': args.npm_react_debug_site,
        'CERTIFICATE': args.certificate,
        'CERTIFICATE_KEY': args.certificate_key,
        'TIMESHIFT_DURATION': str(args.timeshift_duration),
        'CONFIGURATION_RELOAD_TIMEOUT': str(args.configuration_reload_timeout),
        'CONFIG_PATH': args.config_path,
        'EQUALIZER_CONFIG_PATH': args.equalizer_config_path,
    }
    
    for env_var, value in env_mappings.items():
        if env_var not in os.environ:
            os.environ[env_var] = value
    
    return args


def main():
    # Parse arguments and set environment variables before any imports that use constants
    parse_arguments()
    
    # Verify SSL certificates exist, generate if missing
    cert_missing = not os.path.isfile(constants.CERTIFICATE)
    key_missing = not os.path.isfile(constants.CERTIFICATE_KEY)
    
    if cert_missing or key_missing:
        logger.warning("SSL certificate or key not found, generating self-signed certificate...")
        try:
            generate_self_signed_certificate(
                cert_path=constants.CERTIFICATE,
                key_path=constants.CERTIFICATE_KEY
            )
        except Exception as e:
            logger.error(f"Failed to generate self-signed certificate: {e}")
            logger.error("Please manually create the certificate files or check permissions.")
            sys.exit(1)
    else:
        logger.info(f"SSL certificate found: {constants.CERTIFICATE}")
        logger.info(f"SSL certificate key found: {constants.CERTIFICATE_KEY}")
    
    try:
        os.nice(-15)
    except:
        pass


    # logger = get_logger(__name__) # Moved up

    threading.current_thread().name = "ScreamRouter Main Thread"
    main_pid: int = os.getpid()
    ctrl_c_pressed: int = 0

    def signal_handler(_signal, __):
        """Fired when Ctrl+C pressed"""
        if _signal == 2:
            if os.getpid() != main_pid:
                logger.error("Ctrl+C on non-main PID %s", os.getpid())
                return
            logger.error("Ctrl+C pressed")
            website.stop()
            try:
                # Stop components that have stop methods
                if 'screamrouter_configuration' in locals() and hasattr(screamrouter_configuration, 'stop'):
                    screamrouter_configuration.stop()
                # Stop the manual PTR responder
                if 'manual_ptr_responder' in locals() and hasattr(manual_ptr_responder, 'stop'):
                    manual_ptr_responder.stop()
                # Stop the NTP server process
                if 'ntp_server' in locals() and hasattr(ntp_server, 'stop'):
                    ntp_server.stop()
            except Exception as e:
                logger.error(f"Error during signal handler cleanup: {e}")
            server.should_exit = True
            server.force_exit = True
            os.kill(os.getpid(), signal.SIGTERM)
            sys.exit(0)


    signal.signal(signal.SIGINT, signal_handler)

    app: FastAPI = FastAPI( title="ScreamRouter",
            description = "Routes PCM audio around for Scream sinks and sources",
            version="0.0.1",
            contact={
                "name": "ScreamRouter",
                "url": "http://github.com/netham45/screamrouter",
            },
            license_info={
                "name": "No license chosen yet, all rights reserved",
            },
            openapi_tags=[
            {
                "name": "Sink Configuration",
                "description": "API endpoints for managing Sinks"
            },
            {
                "name": "Source Configuration",
                "description": "API endpoints for managing Sources"
            },
            {
                "name": "Route Configuration",
                "description": "API endpoints for managing Routes"
            },
            {
                "name": "Site",
                "description": "File handlers for the site interface"
            },
            {
                "name": "Stream",
                "description": "HTTP media streams"
            }
        ])
    set_process_name("SR Scream Router", "Scream Router Main Thread")

    # Instantiate AudioManager
    audio_manager: AudioManager = AudioManager()

    # Get configuration for AudioManager
    py_audio_manager_config = AudioManagerConfig()
    global_buffer_duration = py_audio_manager_config.global_timeshift_buffer_duration_sec
    # Default rtp_listen_port, as per previous comment in code.
    # This should ideally be a constant or more formally configured if it needs to change.
    default_rtp_port = 40000

    # Initialize AudioManager
    # Refer to bindings.cpp for exact signature. This call assumes bindings will be updated for:
    # py::arg("rtp_listen_port"), py::arg("global_timeshift_buffer_duration_sec")
    if not audio_manager.initialize(rtp_listen_port=default_rtp_port, global_timeshift_buffer_duration_sec=global_buffer_duration):
        logger.error(
            "Failed to initialize AudioManager with rtp_listen_port=%s and global_timeshift_buffer_duration_sec=%s. Exiting.",
            default_rtp_port, global_buffer_duration
        )
        sys.exit(1)
    else:
        logger.info(
            "AudioManager initialized successfully with rtp_listen_port=%s and global_timeshift_buffer_duration_sec=%s seconds.",
            default_rtp_port, global_buffer_duration
        )

    webstream: APIWebStream = APIWebStream(app, audio_manager) # Pass audio_manager
    websocket_config: APIWebsocketConfig = APIWebsocketConfig(app)
    websocket_debug: APIWebsocketDebug = APIWebsocketDebug(app)
    # Note: webrtc_api will be initialized after configuration_manager is created
    plugin_manager: PluginManager = PluginManager(app, audio_manager_instance=audio_manager)
    plugin_manager.start_registered_plugins()

    # --- mDNS Setup ---
    # Extract hostname from SSL certificate
    cert_hostname = "screamrouter.local."  # Default fallback
    cert_hostname_base = "screamrouter"    # Base hostname without .local.
    try:
        # Read the certificate file
        with open(constants.CERTIFICATE, 'rb') as cert_file:
            cert_data = cert_file.read()
        
        # Parse the certificate
        cert = OpenSSL.crypto.load_certificate(OpenSSL.crypto.FILETYPE_PEM, cert_data)
        
        # Extract Subject Alternative Names (SAN)
        san_ext = None
        for i in range(cert.get_extension_count()):
            ext = cert.get_extension(i)
            if ext.get_short_name() == b'subjectAltName':
                san_ext = str(ext)
                break
        
        if san_ext:
            # Parse the SAN extension
            # Format is typically: "DNS:example.com, DNS:www.example.com, ..."
            sans = [name.strip().split(':')[1] for name in san_ext.split(',') 
                    if name.strip().startswith('DNS:')]
            if sans:
                cert_hostname_base = sans[0]  # Use the first DNS name
                # Remove .local. if present for base hostname
                if cert_hostname_base.endswith('.local.'):
                    cert_hostname_base = cert_hostname_base[:-7]  # Remove .local.
                elif cert_hostname_base.endswith('.local'):
                    cert_hostname_base = cert_hostname_base[:-6]  # Remove .local
                    
                cert_hostname = f"{cert_hostname_base}.local."
                logger.info(f"Using hostname from SSL certificate: {cert_hostname}")
        
        # If no SAN, try Common Name (CN)
        if cert_hostname == "screamrouter.local.":
            subject = cert.get_subject()
            if hasattr(subject, 'CN') and subject.CN:
                cert_hostname_base = subject.CN
                # Remove .local. if present for base hostname
                if cert_hostname_base.endswith('.local.'):
                    cert_hostname_base = cert_hostname_base[:-7]  # Remove .local.
                elif cert_hostname_base.endswith('.local'):
                    cert_hostname_base = cert_hostname_base[:-6]  # Remove .local
                    
                cert_hostname = f"{cert_hostname_base}.local."
                logger.info(f"Using CN from SSL certificate: {cert_hostname}")
    except Exception as e:
        logger.error(f"Error extracting hostname from certificate: {e}")
        logger.warning("Using default hostname: screamrouter.local.")

    # Get the IP address of the local machine
    # This is the IP address that Uvicorn is listening on
    import socket

    local_ip = None
    try:
        # Get the IP address that would be used to connect to an external host
        # This avoids getting the loopback address (127.0.0.1)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # Doesn't actually connect, just sets up the socket
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
        logger.info(f"Detected local IP address: {local_ip}")
    except Exception as e:
        logger.error(f"Error detecting local IP address: {e}")
        # Fallback: try to get all non-loopback addresses
        try:
            hostname = socket.gethostname()
            ip_list = socket.gethostbyname_ex(hostname)[2]
            # Filter out loopback addresses
            ip_list = [ip for ip in ip_list if not ip.startswith("127.")]
            if ip_list:
                local_ip = ip_list[0]  # Use the first non-loopback address
                logger.info(f"Using fallback IP address: {local_ip}")
        except Exception as e2:
            logger.error(f"Error in fallback IP detection: {e2}")

    # If we still don't have an IP, use the one specified in constants.API_HOST if it's not 0.0.0.0
    if not local_ip:
        if constants.API_HOST and constants.API_HOST != "0.0.0.0":
            local_ip = constants.API_HOST
            logger.info(f"Using API_HOST as IP address: {local_ip}")
        else:
            # Last resort fallback - use a generic loopback address
            # This won't work for external access but at least avoids hardcoding a specific IP
            local_ip = "127.0.0.1"
            logger.warning(f"Could not determine local IP, using loopback address: {local_ip}")
            logger.warning("PTR responses will only work for local lookups!")

    # Start the manual PTR responder with the hostname from the certificate and detected IP
    manual_ptr_responder = ManualPTRResponder(target_ip=local_ip, target_hostname=cert_hostname)
    manual_ptr_responder.start()
    logger.info(f"Started ManualPTRResponder for IP {local_ip} -> {cert_hostname}")
    # --- End mDNS Setup ---

    # --- NTP Server Setup ---
    ntp_server = NTPServerProcess()
    ntp_server.start()
    # --- End NTP Server Setup ---

    screamrouter_configuration: ConfigurationManager = ConfigurationManager(webstream,
                                                                            plugin_manager,
                                                                            websocket_config,
                                                                            audio_manager)
    
    # Now initialize webrtc_api with configuration_manager
    webrtc_api: APIWebRTC = APIWebRTC(app, audio_manager, screamrouter_configuration)

    api_controller = APIConfiguration(app, screamrouter_configuration)
    website: APIWebsite = APIWebsite(app, screamrouter_configuration)
    equalizer: APIEqualizer = APIEqualizer(app)
    stats_api: APIStats = APIStats(app, screamrouter_configuration)

    @app.on_event("startup")
    async def on_startup():
        """
        Event handler for application startup.
        """
        logger.info("Application starting up...")
        # Start background tasks that require a running event loop
        webrtc_api.start_background_tasks()
        logger.info("WebRTC API background tasks started.")

    config = uvicorn.Config(app=app,
                            port=constants.API_PORT,
                            host=constants.API_HOST,
                            log_config=constants.UVICORN_LOG_CONFIG_PATH if constants.LOG_TO_FILE else None,
                            timeout_keep_alive=30,
                            ssl_keyfile=constants.CERTIFICATE_KEY,
                            ssl_certfile=constants.CERTIFICATE)
    server = uvicorn.Server(config)
    print(f"ssl_keyfile: {constants.CERTIFICATE_KEY} ssl_certfile: {constants.CERTIFICATE}")
    server.run()


if __name__ == "__main__":
    main()