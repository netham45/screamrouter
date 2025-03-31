"""mDNS pinger for discovering sources and sinks"""
import threading
import logging
import time
from typing import Optional, Set
from zeroconf import Zeroconf, DNSQuestion, DNSOutgoing, DNSRecord, DNSIncoming, current_time_millis, ServiceBrowser, ServiceListener

# DNS record types and flags
DNS_TYPE_A = 1  # A record type (IPv4 address)
DNS_FLAGS_QR_QUERY = 0x0000  # Standard query

logger = logging.getLogger(__name__)

class ScreamListener(ServiceListener):
    """Listener for Scream mDNS services"""
    def __init__(self):
        self.source_ips: Set[str] = set()
        self.sink_ips: Set[str] = set()
        
    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Handle new services"""
        info = zc.get_service_info(type_, name)
        if info and info.addresses:
            ip = '.'.join(str(x) for x in info.addresses[0])
            if "source.scream" in name:
                if ip not in self.source_ips:
                    logger.info(f"Discovered new source: {ip}")
                    self.source_ips.add(ip)
            elif "sink.scream" in name:
                if ip not in self.sink_ips:
                    logger.info(f"Discovered new sink: {ip}")
                    self.sink_ips.add(ip)
                    
    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Handle service updates"""
        self.add_service(zc, type_, name)
        
    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Handle service removals - we don't remove IPs as they might still be valid"""
        pass

class MDNSPinger:
    """Discovers Scream sources and sinks using mDNS"""
    def __init__(self, shared=None, interval: float = 5.0):
        """Initialize MDNSPinger
        
        Args:
            shared: Ignored (kept for backward compatibility)
            interval: How often to refresh services in seconds
        """
        self.interval = interval
        self.running = False
        self.thread: Optional[threading.Thread] = None
        self.zeroconf: Optional[Zeroconf] = None
        
        # Create listener for handling responses
        self.listener = ScreamListener()
        
        logger.debug("Initializing Zeroconf mDNS handler")
        
    def start(self):
        """Start the mDNS discovery"""
        if self.running:
            return
            
        self.running = True
        self.zeroconf = Zeroconf()
        
        # Create service browser to listen for responses
        self.browser = ServiceBrowser(self.zeroconf, "_scream._udp.local.", self.listener)
        
        # Start refresh thread
        self.thread = threading.Thread(target=self._run)
        self.thread.daemon = True
        self.thread.start()
        
        logger.info("mDNS discovery started")
        
    def stop(self):
        """Stop the mDNS discovery"""
        self.running = False
        if self.thread:
            self.thread.join()
        if self.zeroconf:
            self.zeroconf.close()
            self.zeroconf = None
        logger.info("mDNS discovery stopped")
        
    def get_source_ips(self) -> Set[str]:
        """Get set of discovered source IPs"""
        return self.listener.source_ips.copy()
        
    def get_sink_ips(self) -> Set[str]:
        """Get set of discovered sink IPs"""
        return self.listener.sink_ips.copy()
    
    def _process_packet(self, data: bytes, addr: tuple) -> None:
        """Process an mDNS packet (for compatibility with MDNSShared)"""
        # This is a no-op since we're using zeroconf's listener now
        pass
        
    def _send_query(self, hostname: str) -> None:
        """Send an mDNS query and handle responses"""
        if not self.zeroconf:
            return
            
        # Create and send query
        out = DNSOutgoing(DNS_FLAGS_QR_QUERY)
        out.add_question(DNSQuestion(hostname, DNS_TYPE_A, 1))
        self.zeroconf.send(out)
        
        # No need to process responses manually - the listener will handle them
        
    def _run(self):
        """Main loop that sends queries"""
        while self.running:
            try:
                # Send queries for source.scream and sink.scream
                if self.zeroconf:
                    self._send_query("source.scream")
                    self._send_query("sink.scream")
                
                # Wait for next interval
                logger.debug(f"Sleeping for {self.interval} seconds")
                time.sleep(self.interval)

            except Exception as e:
                if self.running:
                    logger.exception("Error in mDNS refresh loop")
                    time.sleep(1)  # Avoid tight loop on error
