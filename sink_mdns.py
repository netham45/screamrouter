#!/usr/bin/python3
import socket
import threading
import logging
import time
from zeroconf import Zeroconf, ServiceInfo, DNSQuestion, DNSOutgoing, DNSRecord, DNSIncoming

# Set up logging
logging.basicConfig(
    level=logging.DEBUG,  # Set to DEBUG to see all packet details
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class SinkResponder:
    def __init__(self):
        self.hostname = "sink.scream"
        self.running = False
        self.thread = None
        self.zeroconf = None
        
        # Get all local IP addresses
        self.local_ips = self._get_all_ips()
        logger.debug(f"Local IP addresses: {self.local_ips}")
        
    def start(self):
        """Start the mDNS responder in a background thread"""
        if self.running:
            return
            
        self.running = True
        
        # Initialize Zeroconf with all interfaces
        self.zeroconf = Zeroconf(interfaces=self.local_ips)
        
        # Register service info
        self.register_service()
        
        # Start listener thread for direct hostname queries
        self.thread = threading.Thread(target=self._run)
        self.thread.daemon = True
        self.thread.start()
        
        logger.info("Sink mDNS responder started")
        
    def stop(self):
        """Stop the mDNS responder"""
        self.running = False
        
        if self.zeroconf:
            self.zeroconf.unregister_all_services()
            self.zeroconf.close()
            self.zeroconf = None
            
        if self.thread:
            self.thread.join()
            
        logger.info("Sink mDNS responder stopped")
        
    def register_service(self):
        """Register the sink service with Zeroconf"""
        # Create service info with all IP addresses
        addresses = [socket.inet_aton(ip) for ip in self.local_ips]
        service_info = ServiceInfo(
            "_scream._udp.local.",
            f"{self.hostname}._scream._udp.local.",
            addresses=addresses,
            port=5353,
            properties={
                'type': 'sink'
            }
        )
        
        # Register service
        self.zeroconf.register_service(service_info)
        logger.info(f"Registered service {self.hostname} with IPs {self.local_ips}")
        
        # Also register the raw hostname for direct queries
        self._register_hostname()
        
    def _register_hostname(self):
        """Register the raw hostname for direct queries"""
        # This is handled by the listener thread
        pass
        
    def _handle_query(self, msg):
        """Handle a DNS query"""
        for question in msg.questions:
            if question.name.lower() == self.hostname.lower():
                logger.debug(f"Received query for {self.hostname}")
                
                # Create response
                out = DNSOutgoing(0x8400)  # Response flag
                
                # Add answer
                out.add_answer(
                    msg,
                    question.name,
                    1,  # A record
                    1,  # IN class
                    60 * 60,  # TTL (1 hour)
                    socket.inet_aton(self.local_ips[0])  # Use first IP for responses
                )
                
                # Send response
                self.zeroconf.send(out)
                logger.info(f"Responded to query for {self.hostname} with IP {self.local_ips[0]}")
        
    def _run(self):
        """Main loop that listens for direct hostname queries"""
        while self.running:
            try:
                # Create a query for our own hostname to keep it in the cache
                out = DNSOutgoing(0x0000)  # Query flag
                out.add_question(DNSQuestion(self.hostname, 1, 1))  # A record, IN class
                self.zeroconf.send(out)
                
                # Sleep for a while
                time.sleep(60)  # Refresh every minute
                
            except Exception as e:
                if self.running:  # Only log if we're supposed to be running
                    logger.exception("Error in mDNS responder thread")
                    time.sleep(1)  # Avoid tight loop on error
    
    def _get_all_ips(self):
        """Get all local IP addresses"""
        ips = set()  # Use a set to avoid duplicates
        
        try:
            # Method 1: Use socket.gethostbyname_ex
            try:
                hostname = socket.gethostname()
                _, _, ip_list = socket.gethostbyname_ex(hostname)
                for ip in ip_list:
                    if ip != '127.0.0.1':
                        ips.add(ip)
            except socket.gaierror:
                pass
                
            # Method 2: Try to connect to an external address
            if not ips:
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                try:
                    s.connect(('8.8.8.8', 1))  # Google DNS
                    ips.add(s.getsockname()[0])
                except Exception:
                    pass
                finally:
                    s.close()
                    
            # Fallback to localhost if nothing else works
            if not ips:
                ips.add('127.0.0.1')
                
        except Exception as e:
            logger.exception("Error getting IP addresses")
            ips.add('127.0.0.1')
            
        return list(ips)  # Convert back to list

    def __del__(self):
        """Ensure resources are cleaned up on deletion"""
        self.stop()

if __name__ == "__main__":
    # Create and start responder
    responder = SinkResponder()
    responder.start()
    
    # Keep main thread alive
    logger.info("Sink mDNS responder running. Press Ctrl+C to exit.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        responder.stop()
