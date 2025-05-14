"""mDNS pinger for discovering sources and sinks"""
import logging
import threading
import time
from typing import Optional, Set

from zeroconf import (DNSIncoming, DNSOutgoing, DNSQuestion, DNSRecord,
                      RecordUpdateListener, ServiceBrowser, ServiceListener,
                      Zeroconf, current_time_millis)

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
            if "_source._scream._udp.local" in name:
                if ip not in self.source_ips:
                    #logger.info(f"Discovered new source: {ip}")
                    self.source_ips.add(ip)
            elif "_sink._scream._udp.local" in name:
                if ip not in self.sink_ips:
                    #logger.info(f"Discovered new sink: {ip}")
                    self.sink_ips.add(ip)
                    
    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Handle service updates"""
        self.add_service(zc, type_, name)
        
    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Handle service removals - we don't remove IPs as they might still be valid"""
        pass

class MDNSPinger:
    """Discovers Scream sources and sinks using mDNS"""
    def __init__(self, interval: float = 5.0):
        """Initialize MDNSPinger
        
        Args:
            interval: How often to refresh services in seconds
        """
        self.interval = interval
        self.running = False
        self.thread: Optional[threading.Thread] = None
        self.zeroconf: Optional[Zeroconf] = None
        
        # Create listener for handling responses
        self.listener = ScreamListener()
        
        #logger.debug("Initializing Zeroconf mDNS handler")
        
    def start(self):
        """Start the mDNS discovery"""
        if self.running:
            return
            
        self.running = True
        self.zeroconf = Zeroconf()
        
        # Start refresh thread
        self.thread = threading.Thread(target=self._run)
        self.thread.daemon = True
        self.thread.start()
        
        #logger.info("mDNS discovery started")
        
    def stop(self):
        """Stop the mDNS discovery"""
        self.running = False
        if self.thread:
            self.thread.join()
        if self.zeroconf:
            self.zeroconf.close()
            self.zeroconf = None
        #logger.info("mDNS discovery stopped")
        
    def get_source_ips(self) -> Set[str]:
        """Get set of discovered source IPs"""
        return self.listener.source_ips.copy()
        
    def get_sink_ips(self) -> Set[str]:
        """Get set of discovered sink IPs"""
        return self.listener.sink_ips.copy()
    
    def update_record(self, zeroconf: Zeroconf, now: float, record: DNSRecord) -> None:
        """Handle record updates from Zeroconf"""
        #logger.info(f"Received record update: {record}")
        
        if record.type == DNS_TYPE_A:  # A record
            name = record.name.lower()
            #logger.info(f"Processing A record for {name}")
            
            if hasattr(record, 'address'):
                if isinstance(record.address, bytes):
                    ip = '.'.join(str(x) for x in record.address)
                else:
                    ip = record.address
                
                #logger.info(f"A record IP: {ip}")
                
                # Add the IP to the appropriate set regardless of the name
                # This ensures we capture all responses
                if "_source" in name:
                    if ip not in self.listener.source_ips:
                        #logger.info(f"Discovered new source via A record: {ip}")
                        self.listener.source_ips.add(ip)
                elif "_sink" in name:
                    if ip not in self.listener.sink_ips:
                        #logger.info(f"Discovered new sink via A record: {ip}")
                        self.listener.sink_ips.add(ip)
                else:
                    pass
                    #logger.info(f"Unrecognized service name: {name}")
        
    def _send_query(self, hostname: str) -> None:
        """Send an mDNS query and handle responses"""
        if not self.zeroconf:
            return
            
        # Create and send query
        out = DNSOutgoing(DNS_FLAGS_QR_QUERY)
        out.add_question(DNSQuestion(hostname, DNS_TYPE_A, 1))
        self.zeroconf.send(out)
        
        # Wait a bit for responses to arrive
        time.sleep(0.1)
        
        # Register a handler for incoming packets
        class ResponseHandler(RecordUpdateListener):
            def __init__(self, pinger):
                self.pinger = pinger
                
            def update_record(self, zeroconf, now, record):
                if record.type == DNS_TYPE_A:  # A record
                    name = record.name.lower()
                    #logger.info(f"Found A record for {name}")
                    
                    if hasattr(record, 'address'):
                        if isinstance(record.address, bytes):
                            ip = '.'.join(str(x) for x in record.address)
                        else:
                            ip = record.address
                        
                        #logger.info(f"A record IP: {ip}")
                        
                        # Add the IP to the appropriate set
                        if "_source" in name:
                            if ip not in self.pinger.listener.source_ips:
                                #logger.info(f"Discovered new source via A record: {ip}")
                                self.pinger.listener.source_ips.add(ip)
                        elif "_sink" in name:
                            if ip not in self.pinger.listener.sink_ips:
                                #logger.info(f"Discovered new sink via A record: {ip}")
                                self.pinger.listener.sink_ips.add(ip)
        
        # Create and register the handler
        handler = ResponseHandler(self)
        self.zeroconf.add_listener(handler, None)
        
    def _run(self):
        """Main loop that sends queries"""
        while self.running:
            try:
                # Send queries for source.scream and sink.scream
                if self.zeroconf:
                    self._send_query("_source._scream._udp.local.")
                    self._send_query("_sink._scream._udp.local.")
                
                # Wait for next interval
                #logger.debug(f"Sleeping for {self.interval} seconds")
                time.sleep(self.interval)

            except Exception as e:
                if self.running:
                    #logger.exception("Error in mDNS refresh loop")
                    time.sleep(1)  # Avoid tight loop on error
