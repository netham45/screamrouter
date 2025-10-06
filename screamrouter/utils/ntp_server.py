import os
import signal
import socket
import struct
import threading
import time
from threading import Thread

from ..screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)

# NTP constants
NTP_PORT = 123
NTP_PACKET_FORMAT = "!B B B b 11I"
NTP_DELTA = 2208988800  # Time difference between NTP epoch (1900) and Unix epoch (1970)

class NTPServerProcess(Thread):
    """Manages the NTP server running in a separate process."""
    def __init__(self):
        super().__init__(daemon=True, name="SR NTP Server")
        logger.info(f"Init NTP server process (PID: {os.getpid()}) on port {NTP_PORT}")
        self._server_socket = None

    def run(self):
        """The function run by the thread process."""
        logger.info(f"Starting NTP server process (PID: {os.getpid()}) on port {NTP_PORT}")
        try:
            self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            # Allow address reuse immediately after the socket is closed
            self._server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._server_socket.bind(('0.0.0.0', NTP_PORT))
            logger.info(f"NTP server listening on 0.0.0.0:{NTP_PORT}")

            while True:
                try:
                    data, addr = self._server_socket.recvfrom(1024)
                    if not data or len(data) < 48: # Basic validation
                        logger.warning(f"Received invalid NTP request from {addr}")
                        continue

                    recv_time = time.time() + NTP_DELTA

                    # Construct server response
                    transmit_time = time.time() + NTP_DELTA

                    # Extract originate timestamp from client packet (offset 40, 8 bytes)
                    originate_timestamp_secs = struct.unpack('!I', data[40:44])[0]
                    originate_timestamp_frac = struct.unpack('!I', data[44:48])[0]

                    response_packet = struct.pack(NTP_PACKET_FORMAT,
                                                  (0 << 6 | 4 << 3 | 4),  # LI, VN, Mode
                                                  2,                      # Stratum
                                                  4,                      # Poll
                                                  -6,                     # Precision (log2 seconds)
                                                  0,                      # Root Delay
                                                  0,                      # Root Dispersion
                                                  int.from_bytes(b'LOCL', 'big'), # Reference ID
                                                  0, 0,                   # Reference Timestamp (seconds, fraction)
                                                  originate_timestamp_secs, # Originate Timestamp (seconds)
                                                  originate_timestamp_frac, # Originate Timestamp (fraction)
                                                  int(recv_time),         # Receive Timestamp (seconds)
                                                  int((recv_time % 1) * (2**32)), # Receive Timestamp (fraction)
                                                  int(transmit_time),     # Transmit Timestamp (seconds)
                                                  int((transmit_time % 1) * (2**32)) # Transmit Timestamp (fraction)
                                                 )

                    self._server_socket.sendto(response_packet, addr)
                    #logger.debug(f"Sent NTP response to {addr}")

                except socket.timeout:
                    continue # Go back to waiting
                except OSError as e:
                    # Handle cases like the socket being closed during shutdown
                    if e.errno == 9: # Bad file descriptor (likely closed)
                        logger.info("NTP server socket closed.")
                        break
                    else:
                        logger.error(f"NTP server socket error: {e}")
                        break # Exit on other errors
                except Exception as e:
                    logger.error(f"Error in NTP server loop: {e}")
                    # Decide whether to continue or break based on the error
                    # For now, let's break on unexpected errors
                    break

        except Exception as e:
            logger.error(f"Failed to start NTP server: {e}")
        finally:
            if self._server_socket:
                self._server_socket.close()
            logger.info("NTP server process stopped.")

    def stop(self):
        """Stops the NTP server process."""
        logger.info("Stopping NTP server...")
        try:
            # Close the socket to interrupt recvfrom
            if self._server_socket:
                self._server_socket.close()

            # Wait a short time for graceful exit
            self.join(timeout=2)

            # If still alive, log warning
            if self.is_alive():
                logger.warning("NTP server did not terminate gracefully, waiting longer...")
                self.join(timeout=1)

            # Final check
            if self.is_alive():
                logger.error("Failed to terminate NTP server thread.")
            else:
                logger.info("NTP server stopped successfully.")

        except Exception as e:
            logger.error(f"Error stopping NTP server: {e}")
