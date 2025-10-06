"""mDNS advertiser for Screamrouter _scream._udp service"""
import logging
import socket
import threading
import uuid
from typing import Optional

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
    
    def __init__(self, port: int = 40000):
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
        self.hostname = self._format_hostname()
        self.service_name = f"{self.hostname}.{self.service_type}"
        
        logger.info(f"ScreamAdvertiser initialized. Port: {port}, MAC: {self.mac_address}, Hostname: {self.hostname}")
    
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
                mac_num = self._get_mac_from_interface()
            
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
    
    def _get_mac_from_interface(self) -> int:
        """
        Fallback method to get MAC address by examining network interfaces.
        
        Returns:
            MAC address as integer
        """
        try:
            import netifaces
            
            # Get all network interfaces
            interfaces = netifaces.interfaces()
            
            for interface in interfaces:
                # Skip loopback
                if interface == 'lo':
                    continue
                    
                # Get addresses for this interface
                addrs = netifaces.ifaddresses(interface)
                
                # Check if it has a MAC address (AF_LINK)
                if netifaces.AF_LINK in addrs:
                    mac_info = addrs[netifaces.AF_LINK][0]
                    if 'addr' in mac_info:
                        mac_str = mac_info['addr']
                        # Convert to integer
                        mac_int = int(mac_str.replace(':', ''), 16)
                        logger.debug(f"Got MAC from interface {interface}: {mac_str}")
                        return mac_int
                        
        except ImportError:
            logger.warning("netifaces not available for MAC detection")
        except Exception as e:
            logger.error(f"Error getting MAC from interfaces: {e}")
        
        # If all else fails, return a generated number
        return uuid.getnode()
    
    def _format_hostname(self) -> str:
        """
        Format the hostname as Screamrouter-<last-six-of-MAC>.
        
        Returns:
            Formatted hostname (e.g., "Screamrouter-112B00")
        """
        # Get last 6 characters of MAC address (without colons)
        mac_clean = self.mac_address.replace(':', '')
        mac_suffix = mac_clean[-6:]
        
        hostname = f"Screamrouter-{mac_suffix}"
        logger.debug(f"Formatted hostname: {hostname}")
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
        properties = {
            "mode": "receiver",
            "type": "receiver",
            "mac": self.mac_address,
            "samplerates": "44100,48000",
            "codecs": "lpcm",
            "channels": "8"
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