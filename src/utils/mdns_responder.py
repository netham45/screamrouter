import socket
import struct
import logging
from typing import Optional
from src.utils.mdns_shared import MDNSShared

logger = logging.getLogger(__name__)

class MDNSResponder:
    def __init__(self, shared: 'MDNSShared'):
        """Initialize MDNSResponder with shared mDNS handler
        
        Args:
            shared: Shared mDNS socket and queues
        """
        self.hostname = b"router.scream"
        self.shared = shared
        #logger.debug("Using shared mDNS handler")
        
    def _process_query(self, data: bytes, addr: tuple):
        """Process an mDNS query packet"""
        try:
            # Extract query data from DNS message
            header = struct.unpack("!HHHHHH", data[:12])
            id, flags, qd_count, an_count, ns_count, ar_count = header
            
            # Check if this is a query (QR bit = 0)
            if (flags & 0x8000) != 0:  # Response bit set
                return
                
            # Skip header
            pos = 12
            
            # Parse question section
            name = []
            while pos < len(data):
                length = data[pos]
                if length == 0:
                    break
                pos += 1
                name.append(data[pos:pos+length])
                pos += length
            
            qname = b".".join(name)
            #logger.debug(f"Query name: {qname.decode()}")
            
            # Skip past null byte and get question type/class
            pos += 1
            if pos + 4 > len(data):
                #logger.debug("Query truncated")
                return
                
            qtype, qclass = struct.unpack("!HH", data[pos:pos+4])
            #logger.debug(f"Query type={qtype}, class={qclass}")
            
            # Only respond to A record queries for our hostname
            if qtype == 1 and qclass == 1 and qname == self.hostname:
                # Get the IP that received this query by creating a socket
                # to the sender's address
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                try:
                    sock.connect(addr)
                    my_ip = sock.getsockname()[0]
                    #logger.debug(f"Query received on interface with IP {my_ip}")
                finally:
                    sock.close()
                
                # Craft DNS response
                # Header: ID, Flags, QDCount, ANCount, NSCount, ARCount
                response = struct.pack("!HHHHHH",
                    header[0],    # Same ID as query
                    0x8400,       # Standard response with RA bit
                    qd_count,     # Question count
                    1,            # Answer count
                    0,            # Authority count
                    0             # Additional count
                )
                
                # Question section (copy from query)
                response += data[12:pos+4]
                
                # Answer section
                response += (
                    b"\xc0\x0c"  # Pointer to hostname in question
                    b"\x00\x01"  # Type A
                    b"\x00\x01"  # Class IN
                    b"\x00\x00\x0e\x10"  # TTL (1 hour)
                )
                
                # Add IP length and address
                ip_bytes = bytes([int(x) for x in my_ip.split(".")])
                response += struct.pack("!H", len(ip_bytes))  # RDLENGTH
                response += ip_bytes  # RDATA
                
                # Send response
                self.shared.send(response, addr)
                logger.info(f"Responded to mDNS query for {self.hostname.decode()} with IP {my_ip}")
            else:
                pass
                #logger.debug(f"Ignoring query: wrong type={qtype}, class={qclass}, or name={qname.decode()}")
                
        except Exception as e:
            logger.exception("Error processing mDNS query")
