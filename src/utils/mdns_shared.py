#!/usr/bin/python3
"""Shared mDNS functionality"""

import socket
import struct
import queue
import threading
import logging
from typing import Optional, Tuple

logger = logging.getLogger(__name__)

class MDNSShared:
    """Shared mDNS socket and packet handling"""
    
    def __init__(self):
        """Initialize shared mDNS socket and queues"""
        self.running = False
        self.thread: Optional[threading.Thread] = None
        
        # Create multicast UDP socket
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        
        # Bind to mDNS port
        self.sock.bind(('', 5353))
        #logger.debug("Bound to port 5353")
        
        # Join multicast group
        mreq = struct.pack("4s4s", socket.inet_aton("224.0.0.251"),
                          socket.inet_aton("0.0.0.0"))
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        #logger.debug("Joined multicast group 224.0.0.251")
        
        self.pinger = None
        self.responder = None
        
    def start(self):
        """Start the packet receiver thread"""
        if self.running:
            return
            
        self.running = True
        self.thread = threading.Thread(target=self._run)
        self.thread.daemon = True
        self.thread.start()
        logger.info("mDNS packet receiver started")
        
    def stop(self):
        """Stop the packet receiver thread"""
        self.running = False
        if self.thread:
            self.thread.join()
        self.sock.close()
        logger.info("mDNS packet receiver stopped")
        
    def set_handlers(self, pinger, responder):
        """Set the packet handlers"""
        self.pinger = pinger
        self.responder = responder
        
    def _run(self):
        """Main loop that receives packets and sends to handlers"""
        while self.running:
            try:
                data, addr = self.sock.recvfrom(4096)
                #logger.debug(f"Received {len(data)} bytes from {addr}")
                
                if len(data) < 12:  # DNS header is 12 bytes
                    #logger.debug(f"Packet too short: {len(data)} bytes")
                    continue
                
                # Parse header flags
                flags = struct.unpack("!H", data[2:4])[0]
                #logger.debug(f"Packet flags=0x{flags:04x}, from={addr}, data={data.hex()}")
                
                # Route packets based on QR bit
                if (flags & 0x8000) != 0:  # Response
                    if self.pinger:
                        self.pinger._process_packet(data, addr)
                else:  # Query
                    if self.responder:
                        self.responder._process_query(data, addr)
                    
            except Exception as e:
                if self.running:
                    logger.exception("Error receiving mDNS packet")
                    
    def send(self, data: bytes, addr: tuple):
        """Send a packet using the shared socket"""
        self.sock.sendto(data, addr)
