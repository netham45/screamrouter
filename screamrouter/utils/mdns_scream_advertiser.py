"""mDNS advertiser for Screamrouter _scream._udp service"""
import logging
import socket
import threading
import uuid
from typing import Optional, Sequence

from zeroconf import ServiceInfo, Zeroconf, IPVersion

try:
    from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
except ImportError:
    # Fallback basic logger if import fails
    logging.basicConfig(level=logging.DEBUG)
    get_logger = logging.getLogger

logger = get_logger(__name__)


class ScreamAdvertiser(threading.Thread):
    """
    Advertises Screamrouter as a _scream._udp receiver service via mDNS.
    Compatible with ESP32-Scream devices and other Scream protocol clients.
    """
    
    def __init__(
        self,
        port: int = 40000,
        *,
        sample_rates: Optional[Sequence[int]] = None,
        bit_depths: Optional[Sequence[int]] = None,
        sample_rate: Optional[int] = None,
        bit_depth: Optional[int] = None,
        channels: int = 8,
        channel_layout: Optional[str] = None,
        protocol: str = "scream",
        codecs: str = "lpcm",
    ):
        """
        Initialize the Scream service advertiser.
        
        Args:
            port: The port number where the RTP receiver is listening (default: 40000)
        """
        super().__init__(daemon=True)
        self.name = "ScreamAdvertiserThread"
        
        self.port = port
        self.zeroconf: Optional[Zeroconf] = None
        self.service_info: Optional[ServiceInfo] = None
        self._should_stop = threading.Event()

        # Service configuration
        self.service_type = "_scream._udp.local."
        self.mac_address = self._get_mac_address()
        self.hostname = self._get_local_hostname()
        self.service_name = f"{self.hostname}.{self.service_type}"

        # Audio capability metadata advertised via TXT records
        self.sample_rates = tuple(sample_rates) if sample_rates else (44100, 48000)
        self.sample_rate = sample_rate or (self.sample_rates[-1] if self.sample_rates else 48000)

        self.bit_depths = tuple(bit_depths) if bit_depths else (16, 24, 32)
        self.bit_depth = bit_depth or (self.bit_depths[-1] if self.bit_depths else 32)

        self.channels = channels if channels > 0 else 2
        if channel_layout:
            self.channel_layout = channel_layout
        else:
            layout_map = {
                1: "mono",
                2: "stereo",
                4: "quad",
                6: "5.1",
                7: "6.1",
                8: "7.1",
            }
            self.channel_layout = layout_map.get(self.channels, f"{self.channels}ch")

        self.protocol = protocol or "scream"
        self.codecs = codecs or "lpcm"

        logger.info(
            "ScreamAdvertiser initialized. Port: %s, MAC: %s, Hostname: %s, Channels: %s, SampleRates: %s, BitDepths: %s, Protocol: %s",
            port,
            self.mac_address,
            self.hostname,
            self.channels,
            self.sample_rates,
            self.bit_depths,
            self.protocol,
        )
    
    def _get_mac_address(self) -> str:
        """
        Get the MAC address of the primary network interface.
        
        Returns:
            MAC address as uppercase string with colons (e.g., "AA:BB:CC:DD:EE:FF")
        """
        try:
            # First, try to get MAC using uuid.getnode()
            mac_num = uuid.getnode()
            
            # Check if it's a valid MAC (not a random number)
            # uuid.getnode() returns a random number if it can't get the MAC
            if (mac_num >> 40) % 2:
                # Bit 40 is set, which means it's a randomly generated number
                logger.warning("Could not get real MAC address, using fallback")
                raise RuntimeError("uuid.getnode returned random MAC")
            
            # Format as uppercase hex with colons
            mac_hex = '%012X' % mac_num
            mac_formatted = ':'.join(mac_hex[i:i+2] for i in range(0, 12, 2))
            
            logger.debug(f"Detected MAC address: {mac_formatted}")
            return mac_formatted
            
        except Exception as e:
            logger.error(f"Error getting MAC address: {e}")
            # Return a default/fallback MAC
            fallback = "00:00:00:00:00:00"
            logger.warning(f"Using fallback MAC: {fallback}")
            return fallback
    
    def _get_local_hostname(self) -> str:
        """
        Return the local system hostname suitable for mDNS advertising.
        """
        try:
            hostname = socket.gethostname().split(".")[0]
            hostname = hostname.strip() or "screamrouter"
        except Exception as e:
            logger.error(f"Error determining local hostname: {e}")
            hostname = "screamrouter"
        
        logger.debug(f"Using hostname: {hostname}")
        return hostname
    
    def _get_local_ip(self) -> str:
        """
        Get the local IP address of the primary network interface.
        
        Returns:
            Local IP address as string
        """
        try:
            # Create a dummy socket to determine the local IP
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                # Connect to a public DNS server (doesn't actually send data)
                s.connect(("8.8.8.8", 80))
                local_ip = s.getsockname()[0]
                logger.debug(f"Detected local IP: {local_ip}")
                return local_ip
        except Exception as e:
            logger.error(f"Error getting local IP: {e}")
            # Fallback to localhost if we can't determine
            return "127.0.0.1"
    
    def _create_service_info(self) -> ServiceInfo:
        """
        Create the ServiceInfo object for the _scream._udp service.
        
        Returns:
            Configured ServiceInfo object
        """
        local_ip = self._get_local_ip()
        
        # TXT record properties
        sample_rates_csv = ",".join(str(rate) for rate in self.sample_rates) if self.sample_rates else str(self.sample_rate)
        bit_depths_csv = ",".join(str(depth) for depth in self.bit_depths) if self.bit_depths else str(self.bit_depth)

        properties = {
            "mode": "receiver",
            "type": "receiver",
            "mac": self.mac_address,
            "protocol": self.protocol,
            "protocols": self.protocol,
            "transport": self.protocol,
            "format": self.protocol,
            "codecs": self.codecs,
            "channels": str(self.channels),
            "channel_count": str(self.channels),
            "channelCount": str(self.channels),
            "channel_layout": self.channel_layout,
            "channelLayout": self.channel_layout,
            "sample_rate": str(self.sample_rate),
            "sample_rates": sample_rates_csv,
            "samplerates": sample_rates_csv,
            "supported_sample_rates": sample_rates_csv,
            "supportedSampleRates": sample_rates_csv,
            "sampleRate": str(self.sample_rate),
            "sample_rate_hz": str(self.sample_rate),
            "bit_depth": str(self.bit_depth),
            "bit_depths": bit_depths_csv,
            "supported_bit_depths": bit_depths_csv,
            "supportedBitDepths": bit_depths_csv,
            "bitdepth": str(self.bit_depth),
            "bitDepth": str(self.bit_depth),
            "identifier": self.hostname,
        }

        # Create service info
        service_info = ServiceInfo(
            type_=self.service_type,
            name=self.service_name,
            addresses=[socket.inet_aton(local_ip)],
            port=self.port,
            properties=properties,
            server=f"{self.hostname}.local."
        )
        
        logger.info(f"Created ServiceInfo: name={self.service_name}, port={self.port}, properties={properties}")
        return service_info
    
    def run(self):
        """Main thread execution method for the advertiser."""
        logger.info("ScreamAdvertiser thread starting...")
        
        try:
            # Initialize Zeroconf
            self.zeroconf = Zeroconf(ip_version=IPVersion.V4Only)
            logger.info("Zeroconf initialized for ScreamAdvertiser")
            
            # Create and register the service
            self.service_info = self._create_service_info()
            
            logger.info(f"Registering _scream._udp service: {self.service_name} on port {self.port}")
            self.zeroconf.register_service(self.service_info, allow_name_change=True)
            logger.info(f"Successfully registered _scream._udp service. Clients can now discover {self.hostname}.local on port {self.port}")
            
            # Keep the thread alive
            while not self._should_stop.is_set():
                self._should_stop.wait(timeout=5.0)
            
            logger.info("ScreamAdvertiser stop signal received")
            
        except Exception as e:
            logger.exception(f"Error in ScreamAdvertiser thread: {e}")
            
        finally:
            # Clean up
            if self.zeroconf:
                try:
                    if self.service_info:
                        logger.info("Unregistering _scream._udp service...")
                        self.zeroconf.unregister_service(self.service_info)
                    self.zeroconf.close()
                    logger.info("Zeroconf closed for ScreamAdvertiser")
                except Exception as e:
                    logger.error(f"Error during ScreamAdvertiser cleanup: {e}")
            
            self.zeroconf = None
            self.service_info = None
            logger.info("ScreamAdvertiser thread finished")
    
    def stop(self):
        """Stop the advertiser thread and clean up."""
        logger.info("Stopping ScreamAdvertiser...")
        self._should_stop.set()
        
        if self.is_alive():
            logger.debug("Waiting for ScreamAdvertiser thread to finish...")
            self.join(timeout=10)
            if self.is_alive():
                logger.warning("ScreamAdvertiser thread did not stop cleanly")
            else:
                logger.info("ScreamAdvertiser thread stopped successfully")
        else:
            logger.debug("ScreamAdvertiser thread was not running")


# Example usage for testing
if __name__ == '__main__':
    logging.basicConfig(
        level=logging.INFO,
        format='[%(levelname)s:%(asctime)s][%(threadName)s] %(message)s'
    )
    
    logger.info("Starting ScreamAdvertiser test...")
    advertiser = ScreamAdvertiser(port=40000)
    advertiser.start()
    
    try:
        # Keep running until interrupted
        while advertiser.is_alive():
            advertiser.join(timeout=1.0)
    except KeyboardInterrupt:
        logger.info("KeyboardInterrupt received, stopping advertiser...")
        advertiser.stop()
        logger.info("Test complete")
