import logging
import socket
import threading
from typing import List, Optional

from zeroconf import (InterfaceChoice, IPVersion,  # Import InterfaceChoice
                      ServiceInfo, Zeroconf)
from zeroconf.asyncio import AsyncZeroconf

# Ensure get_logger is imported if not already (assuming it's in the project structure)
# If get_logger is defined elsewhere, adjust the import path accordingly.
# Example: from ..screamrouter_logger.screamrouter_logger import get_logger
# For now, assuming it might be missing or incorrect based on previous errors:
try:
    from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
except ImportError:
    # Fallback basic logger if import fails during standalone testing or structure issues
    logging.basicConfig(level=logging.DEBUG)
    get_logger = logging.getLogger

logger = get_logger(__name__)

# Inherits from threading.Thread
class MDNSResponder(threading.Thread):
    """
    Handles responding to mDNS queries for the hostname using the zeroconf library
    by running Zeroconf in a dedicated thread.
    Registers an A record for screamrouter.local pointing to the local IP address(es)
    and handles PTR records for reverse IP lookups.
    """
    def __init__(self, listen_interfaces: Optional[List[str]] = None, explicit_ips: Optional[List[str]] = None):
        """
        Initialize the MDNSResponder.

        Args:
            listen_interfaces: Optional list of interface IPs to listen on.
                               If None, zeroconf listens on all using InterfaceChoice.All.
            explicit_ips: Optional list of specific IP addresses to register for A and PTR records.
                          If provided, this overrides automatic detection.
        """
        # Initialize the Thread part
        logger.info("MDNSResponder initializing.")
        super().__init__(daemon=False) # Keep non-daemon for now
        self.name = "MDNSResponderThread" # Set thread name

        # MDNSResponder specific attributes
        self.hostname = "screamrouter" # Zeroconf adds .local automatically
        self.service_type = "_screamrouter._tcp.local." # Define a custom service type
        self.service_name = f"{self.hostname}.{self.service_type}"
        self.zeroconf: Optional[Zeroconf] = None
        self.service_info: Optional[ServiceInfo] = None
        self._listen_interfaces = listen_interfaces # Renamed from _interfaces
        self._explicit_ips = explicit_ips # Store explicit IPs
        self._should_stop = threading.Event() # Event to signal stopping
        logger.info(f"MDNSResponder initializing. Listen interfaces: {listen_interfaces}, Explicit IPs: {explicit_ips}")
        # logger.info("MDNSResponder initialized.") # Original log

    def _get_local_ips(self) -> List[str]:
        """Get local IP addresses suitable for registration."""
        ips = []
        try:
            # Get all addresses associated with the hostname
            # Using '' as host allows getting IPs for all interfaces potentially
            addr_info = socket.getaddrinfo(socket.gethostname(), None)
            ips = [info[4][0] for info in addr_info if info[0] == socket.AF_INET] # IPv4 only for now
            # Filter out loopback
            ips = [ip for ip in ips if ip != '127.0.0.1' and not ip.startswith('169.254.')] # Also filter link-local
            logger.info(f"Detected non-loopback/link-local IPv4 addresses via getaddrinfo: {ips}")
        except socket.gaierror as e:
            logger.error(f"Error getting local IP addresses via getaddrinfo: {e}")
            ips = [] # Ensure ips is empty list on error
        except Exception as e:
             logger.exception(f"Unexpected error in _get_local_ips: {e}")
             ips = []

        # Fallback if getaddrinfo failed or returned only unsuitable IPs
        if not ips:
             logger.warning("Could not determine specific local IPs via getaddrinfo. Will rely on Zeroconf's default interface selection unless specific interfaces were provided.")
             return []
        return ips

    # start() is inherited from Thread

    # run() is the standard method executed by start()
    def run(self):
        """The main execution method for the mDNS responder thread."""
        logger.info("Entering MDNSResponder run() method.")
        try:
            # Determine interfaces for Zeroconf to *listen* on
            listen_choice = self._listen_interfaces if self._listen_interfaces is not None else InterfaceChoice.All
            logger.info(f"Attempting to initialize Zeroconf for listening. Interfaces choice: {listen_choice}")
            try:
                self.zeroconf = Zeroconf(ip_version=IPVersion.V4Only, interfaces=listen_choice)
                logger.info("Zeroconf initialized successfully.")
            except OSError as e:
                 # Specific handling for address already in use or permission errors
                 logger.error(f"Failed to initialize Zeroconf due to OS Error (Address/Port likely in use or permissions issue): {e}")
                 return # Stop thread execution
            except Exception as zc_init_e:
                logger.exception(f"Failed to initialize Zeroconf: {zc_init_e}")
                return # Stop thread execution

            # --- Determine IPs for Registration ---
            registration_ips = []
            if self._explicit_ips:
                logger.info(f"Using explicitly provided IPs for registration: {self._explicit_ips}")
                registration_ips = self._explicit_ips
            else:
                logger.info("No explicit IPs provided, attempting automatic detection...")
                local_ips = self._get_local_ips() # Get IPs for registration
                registration_ips = local_ips # Use detected IPs if found
                if not registration_ips:
                     logger.warning("Automatic IP detection failed. Attempting fallback using socket.gethostname().")
                try:
                    hostname_ip = socket.gethostbyname(socket.gethostname())
                    logger.info(f"Fallback hostname resolution: {socket.gethostname()} -> {hostname_ip}")
                    if hostname_ip != '127.0.0.1' and not hostname_ip.startswith('169.254.'):
                        registration_ips = [hostname_ip]
                    else:
                        logger.warning("Hostname resolved to loopback or link-local. Cannot use for registration.")
                        registration_ips = [] # Explicitly empty
                except socket.gaierror as e:
                    logger.error(f"Could not resolve hostname via gethostbyname() for ServiceInfo registration: {e}")
                    registration_ips = []
                except Exception as e:
                    logger.exception(f"Unexpected error during fallback IP resolution: {e}")
                    registration_ips = []

            # Final check if any IPs were determined
            if not registration_ips:
                logger.error("No suitable IP addresses found (explicit, detected, or fallback) for mDNS registration. Aborting.")
                if self.zeroconf: self.zeroconf.close() # Attempt cleanup
                self.zeroconf = None
                return # Stop thread execution

            # Create ServiceInfo
            try:
                # Ensure IPs are valid before creating ServiceInfo
                valid_inet_ips = [socket.inet_aton(ip) for ip in registration_ips]
                if not valid_inet_ips:
                    raise ValueError("No valid IPs after inet_aton conversion.")

                self.service_info = ServiceInfo(
                    type_=self.service_type,
                    name=self.service_name,
                    addresses=valid_inet_ips, # Use validated IPs
                    port=0, # No specific port needed for just hostname resolution
                    properties={}, # No properties needed for now
                    server=f"{self.hostname}.local.", # Explicitly set server name for A record
                )
                logger.info(f"ServiceInfo created for registration: {self.service_info}")
            except Exception as si_e:
                 logger.exception(f"Failed to create ServiceInfo: {si_e}")
                 if self.zeroconf: self.zeroconf.close()
                 self.zeroconf = None
                 return

            # Register service
            logger.info(f"Attempting to register mDNS service: Name='{self.service_name}', Addr='{registration_ips}'")
            try:
                self.zeroconf.register_service(self.service_info, allow_name_change=True)
                logger.info(f"Successfully called register_service for {self.service_name}. Zeroconf should now respond to queries for {self.hostname}.local.")
            except Exception as e:
                logger.exception(f"Failed to register mDNS service '{self.service_name}': {e}")
                if self.zeroconf: self.zeroconf.close()
                self.zeroconf = None
                return # Stop thread execution

            # Keep the thread alive by waiting on the stop event
            logger.info("Entering keep-alive loop (waiting for stop event).")
            while not self._should_stop.is_set():
                # Wait with a timeout allows periodic checks if needed,
                # and makes it responsive to the stop signal.
                self._should_stop.wait(timeout=5.0) # Wait for 5 seconds or until stop is set

            logger.info("Stop event received or loop condition failed.")
            # --- End of correctly indented block ---

        except Exception as e:
            logger.exception(f"Error during MDNSResponder run: {e}")
        finally:
            logger.info("Entering run() finally block.")
            if self.zeroconf:
                logger.info("Unregistering mDNS service and closing Zeroconf.")
                try:
                    # Use the helper method for cleanup
                    self._close_zeroconf(self.zeroconf)
                except Exception as e:
                     # Log error from cleanup attempt, but don't prevent thread exit
                     logger.error(f"Error during Zeroconf cleanup in finally block: {e}")
            self.zeroconf = None # Ensure zeroconf is None after cleanup attempt
            self.service_info = None
            logger.info("MDNS responder thread finished execution.")

    def stop(self):
        """Signals the mDNS responder thread to stop and waits for it to join."""
        logger.info("Attempting to stop MDNS responder thread...")
        self._should_stop.set() # Signal the run() loop to exit

        # No need to call _close_zeroconf here, finally block in run() handles it.

        # Wait for the thread to finish
        if self.is_alive():
            logger.info(f"Waiting for MDNSResponder thread (ID: {self.ident}) to join...")
            self.join(timeout=10) # Wait up to 10 seconds
            if self.is_alive():
                logger.warning(f"MDNSResponder thread (ID: {self.ident}) did not exit cleanly after 10 seconds.")
            else:
                logger.info(f"MDNSResponder thread (ID: {self.ident}) joined successfully.")
        else:
             logger.info("MDNSResponder thread was not alive when stop() was called.")

    def _close_zeroconf(self, zc_instance: Zeroconf):
        """Helper to close zeroconf instance. Safe to call multiple times."""
        if not zc_instance:
             logger.debug("_close_zeroconf called with None instance.")
             return
        try:
            logger.info(f"Executing _close_zeroconf for instance {id(zc_instance)}...")
            # Unregistering might fail if called after close or if already unregistered
            try:
                zc_instance.unregister_all_services()
                logger.info("Zeroconf unregister_all_services completed.")
            except Exception as unreg_e:
                logger.warning(f"Exception during unregister_all_services (might be expected if already closed): {unreg_e}")

            # Closing might fail if called multiple times
            try:
                zc_instance.close()
                logger.info("Zeroconf close() completed.")
            except Exception as close_e:
                logger.warning(f"Exception during close() (might be expected if already closed): {close_e}")

        except Exception as e:
            logger.error(f"Unexpected exception during _close_zeroconf: {e}")

# Example usage (optional, for testing)
if __name__ == '__main__':
    # Setup basic logging for the example
    logging.basicConfig(level=logging.INFO, format='[%(levelname)s:%(asctime)s][%(threadName)s] %(message)s')
    logger.info("Starting MDNSResponder example...")
    responder = MDNSResponder()
    responder.start() # Start the thread

    try:
        # Keep the main thread alive, waiting for KeyboardInterrupt
        while responder.is_alive():
            # Use join with a timeout to allow KeyboardInterrupt check
            responder.join(timeout=1.0)
    except KeyboardInterrupt:
        logger.info("KeyboardInterrupt received, stopping responder...")
