import logging
import socket
import struct
import threading
import time
from typing import Optional

# Ensure get_logger is imported if not already
try:
    from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
except ImportError:
    logging.basicConfig(level=logging.DEBUG)
    get_logger = logging.getLogger

logger = get_logger(__name__)

MDNS_ADDR = '224.0.0.251'
MDNS_PORT = 5353
SOCKET_TIMEOUT = 1.0 # Seconds

def ip_to_reverse_name(ip_address: str) -> str:
    """Converts an IPv4 address string to its reverse lookup domain name."""
    parts = ip_address.split('.')
    if len(parts) != 4:
        raise ValueError("Invalid IPv4 address format")
    return f"{parts[3]}.{parts[2]}.{parts[1]}.{parts[0]}.in-addr.arpa."

def encode_dns_name(name: str) -> bytes:
    """Encodes a domain name into DNS label format."""
    encoded = bytearray()
    for label in name.split('.'):
        if not label: # Handle trailing dot or empty labels
            continue
        length = len(label)
        if length > 63:
            raise ValueError("DNS label exceeds 63 characters")
        encoded.append(length)
        encoded.extend(label.encode('ascii')) # Assuming ASCII hostnames
    encoded.append(0) # Null byte terminator for the name
    return bytes(encoded)

class ManualPTRResponder(threading.Thread):
    """
    Manually listens for and responds to mDNS PTR queries for a specific IP address.
    """
    def __init__(self, target_ip: str, target_hostname: str):
        """
        Initialize the ManualPTRResponder.

        Args:
            target_ip: The IP address whose PTR queries should be answered (e.g., "192.168.3.114").
            target_hostname: The hostname to respond with (e.g., "screamrouter.local.").
        """
        logger.info(f"ManualPTRResponder initializing for IP {target_ip} -> {target_hostname}")
        super().__init__(daemon=False) # Non-daemon to ensure it runs
        self.name = "ManualPTRResponderThread"

        self.target_ip = target_ip
        # Ensure hostname ends with a dot
        self.target_hostname = target_hostname if target_hostname.endswith('.') else target_hostname + '.'
        self.reverse_name_str = ip_to_reverse_name(self.target_ip)
        self.reverse_name_bytes = encode_dns_name(self.reverse_name_str)
        self.target_hostname_bytes = encode_dns_name(self.target_hostname)

        self._should_stop = threading.Event()
        self.sock: Optional[socket.socket] = None
        logger.info(f"ManualPTRResponder initialized. Will respond to PTR queries for {self.reverse_name_str} with {self.target_hostname}")

    def run(self):
        """Listens for mDNS packets and responds to relevant PTR queries."""
        logger.info("Entering ManualPTRResponder run() method.")
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            # Allow reuse of address/port - crucial for multiple listeners
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            # SO_REUSEPORT might be needed on some systems, especially Linux >= 3.9
            try:
                 self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            except AttributeError:
                 logger.warning("SO_REUSEPORT not available on this system.")

            self.sock.bind(('', MDNS_PORT)) # Bind to all interfaces on the mDNS port

            # Join multicast group
            mreq = struct.pack("=4sl", socket.inet_aton(MDNS_ADDR), socket.INADDR_ANY)
            self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
            self.sock.settimeout(SOCKET_TIMEOUT) # Use timeout for non-blocking check of stop flag

            logger.info(f"ManualPTRResponder socket bound and listening on port {MDNS_PORT}")

            while not self._should_stop.is_set():
                try:
                    data, addr = self.sock.recvfrom(1024)
                    self._process_packet(data, addr)
                except socket.timeout:
                    continue # Timeout allows checking _should_stop flag
                except Exception as e:
                    logger.exception(f"Error receiving or processing packet: {e}")
                    time.sleep(0.1) # Avoid busy-loop on persistent errors

        except OSError as e:
            logger.error(f"Failed to bind socket (Address/Port likely in use or permissions issue): {e}")
        except Exception as e:
            logger.exception(f"Critical error in ManualPTRResponder run loop: {e}")
        finally:
            logger.info("Entering ManualPTRResponder run() finally block.")
            if self.sock:
                try:
                    # Leave multicast group
                    mreq = struct.pack("=4sl", socket.inet_aton(MDNS_ADDR), socket.INADDR_ANY)
                    self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_DROP_MEMBERSHIP, mreq)
                except Exception as e:
                    logger.error(f"Error leaving multicast group: {e}")
                try:
                    self.sock.close()
                    logger.info("ManualPTRResponder socket closed.")
                except Exception as e:
                    logger.error(f"Error closing socket: {e}")
            self.sock = None
            logger.info("ManualPTRResponder thread finished execution.")

    def stop(self):
        """Signals the responder thread to stop."""
        logger.info("Attempting to stop ManualPTRResponder thread...")
        self._should_stop.set()
        # No need to join here if called from main thread's signal handler,
        # but joining ensures cleanup if called elsewhere.
        if self.is_alive() and threading.current_thread() != self:
             logger.info(f"Waiting for ManualPTRResponder thread (ID: {self.ident}) to join...")
             self.join(timeout=SOCKET_TIMEOUT + 1) # Wait slightly longer than socket timeout
             if self.is_alive():
                  logger.warning(f"ManualPTRResponder thread (ID: {self.ident}) did not exit cleanly.")
             else:
                  logger.info(f"ManualPTRResponder thread (ID: {self.ident}) joined successfully.")

    def _process_packet(self, data: bytes, addr: tuple):
        """Parses a DNS packet and responds if it's a matching PTR query."""
        try:
            # Basic DNS Header Parsing
            if len(data) < 12:
                logger.debug("Packet too short for DNS header.")
                return
            tid, flags, qdcount, ancount, nscount, arcount = struct.unpack('!HHHHHH', data[:12])

            # Check if it's a standard query (not a response)
            if flags & 0x8000: # QR bit is set (response)
                return

            # --- Parse Question Section ---
            offset = 12
            qname_bytes, offset = self._decode_dns_name(data, offset)
            if qname_bytes is None:
                logger.debug("Failed to decode query name.")
                return

            if offset + 4 > len(data):
                logger.debug("Packet too short for query type/class.")
                return
            qtype, qclass = struct.unpack('!HH', data[offset:offset+4])
            offset += 4

            # --- Check if it's the PTR query we care about ---
            # QTYPE 12 = PTR, QCLASS 1 = IN
            if qtype == 12 and qclass & 0x7FFF == 1 and qname_bytes == self.reverse_name_bytes:
                logger.info(f"Received matching PTR query for {self.reverse_name_str} from {addr}")

                # --- Construct Response ---
                response = bytearray()
                # Header: TID, Flags (Response, Authoritative), QDCOUNT, ANCOUNT, NSCOUNT, ARCOUNT
                # Flags: 1000 0100 0000 0000 = 0x8400 (Response + Recursion Available, Authoritative Answer)
                res_flags = 0x8400
                response.extend(struct.pack('!HHHHHH', tid, res_flags, qdcount, 1, 0, 0))

                # Question Section (copy from query)
                # Find end of question section in original data
                q_section_end = 12
                temp_offset = 12
                for _ in range(qdcount):
                     _, temp_offset = self._decode_dns_name(data, temp_offset)
                     if temp_offset is None or temp_offset + 4 > len(data): break
                     temp_offset += 4
                else: # Only copy if loop completed successfully
                     q_section_end = temp_offset
                response.extend(data[12:q_section_end])

                # Answer Section
                # Name (pointer to question name: C0 0C assumes name starts at offset 12)
                response.extend(b'\xc0\x0c')
                # Type (PTR), Class (IN), TTL (e.g., 120 seconds)
                response.extend(struct.pack('!HHI', 12, 1, 120))
                # RDLENGTH (length of encoded target hostname)
                response.extend(struct.pack('!H', len(self.target_hostname_bytes)))
                # RDATA (encoded target hostname)
                response.extend(self.target_hostname_bytes)

                # Send response
                if self.sock:
                    self.sock.sendto(response, addr)
                    logger.info(f"Sent PTR response: {self.reverse_name_str} -> {self.target_hostname} to {addr}")
                else:
                    logger.warning("Socket not available to send PTR response.")

        except Exception as e:
            logger.exception(f"Error processing packet data: {e}")

    def _decode_dns_name(self, data: bytes, offset: int) -> tuple[Optional[bytes], Optional[int]]:
        """Decodes a domain name from DNS packet data, handling pointers."""
        name_parts = []
        original_offset = offset
        jumped = False

        while offset < len(data):
            length = data[offset]
            if length == 0: # End of name
                offset += 1
                break
            elif (length & 0xC0) == 0xC0: # Pointer
                if offset + 1 >= len(data): return None, None # Invalid pointer
                pointer_offset = struct.unpack('!H', data[offset:offset+2])[0] & 0x3FFF
                if not jumped: # Only update offset on the first jump
                    original_offset = offset + 2
                jumped = True
                offset = pointer_offset # Jump to the pointed-to location
                if offset >= len(data): return None, None # Invalid pointer offset
            elif (length & 0xC0) == 0x00: # Normal label
                offset += 1
                if offset + length > len(data): return None, None # Truncated label
                name_parts.append(data[offset:offset+length])
                offset += length
            else: # Unknown label type
                return None, None

        final_offset = original_offset if jumped else offset
        # Reconstruct name with dots, add trailing dot, encode
        full_name = b'.'.join(name_parts) + b'.'
        return encode_dns_name(full_name.decode('ascii', errors='ignore')), final_offset
