#!/usr/bin/python3
"""ScreamRouter"""
import multiprocessing
import os
import signal
import sys
import threading
import OpenSSL.crypto  # For reading the SSL certificate

import uvicorn
from fastapi import FastAPI

import src.constants.constants as constants
from src.api.api_configuration import APIConfiguration
from src.api.api_website import APIWebsite
from src.api.api_webstream import APIWebStream
from src.api.api_websocket_config import APIWebsocketConfig
from src.api.api_websocket_debug import APIWebsocketDebug
from src.api.api_equalizer import APIEqualizer
from src.configuration.configuration_manager import ConfigurationManager
from src.plugin_manager.plugin_manager import PluginManager
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.utils.utils import set_process_name
from src.utils.mdns_ptr_responder import ManualPTRResponder # mDNS
from src.utils.ntp_server import NTPServerProcess # NTP Server

try:
    os.nice(-15)
except:
    pass

logger = get_logger(__name__)

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
if constants.DEBUG_MULTIPROCESSING:
    logger = multiprocessing.log_to_stderr()
    logger.setLevel(multiprocessing.SUBDEBUG) # type: ignore
webstream: APIWebStream = APIWebStream(app)
websocket_config: APIWebsocketConfig = APIWebsocketConfig(app)
websocket_debug: APIWebsocketDebug = APIWebsocketDebug(app)
plugin_manager: PluginManager = PluginManager(app)
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

# Configuration Manager (no longer needs mDNS responders passed)
screamrouter_configuration: ConfigurationManager = ConfigurationManager(webstream,
                                                                        plugin_manager,
                                                                        websocket_config)

api_controller = APIConfiguration(app, screamrouter_configuration)
website: APIWebsite = APIWebsite(app, screamrouter_configuration)
equalizer: APIEqualizer = APIEqualizer(app)

config = uvicorn.Config(app=app,
                        port=constants.API_PORT,
                        host=constants.API_HOST,
                        log_config="uvicorn_log_config.yaml" if constants.LOG_TO_FILE else None,
                        timeout_keep_alive=30,
                        ssl_keyfile=constants.CERTIFICATE_KEY,
                        ssl_certfile=constants.CERTIFICATE)
server = uvicorn.Server(config)
server.run()
