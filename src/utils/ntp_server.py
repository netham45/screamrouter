import multiprocessing
import socket
import struct
import time
import signal
import os
from ..screamrouter_logger.screamrouter_logger import get_logger
from multiprocessing import Process
from threading import Thread
from logging import getLogger
logger = get_logger(__name__)

# NTP constants
NTP_PORT = 123
NTP_PACKET_FORMAT = "!B B B b 11I"
NTP_DELTA = 2208988800  # Time difference between NTP epoch (1900) and Unix epoch (1970)

class NTPServerProcess(Thread):
    """Manages the NTP server running in a separate process."""
    def __init__(self):
        super().__init__(daemon=True, name="SR NTP Server")
        print(f"Init NTP server process (PID: {os.getpid()}) on port {NTP_PORT}")
        self._server_socket = None

    def run(self):
        """The function run by the multiprocessing process."""
        #signal.signal(signal.SIGTERM, lambda sig, frame: exit(0))

        print(f"Starting NTP server process (PID: {os.getpid()}) on port {NTP_PORT}")
        try:
            self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            # Allow address reuse immediately after the socket is closed
            self._server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._server_socket.bind(('0.0.0.0', NTP_PORT))
            print(f"NTP server listening on 0.0.0.0:{NTP_PORT}")

            while True:
                try:
                    data, addr = self._server_socket.recvfrom(1024)
                    if not data or len(data) < 48: # Basic validation
                        print(f"Received invalid NTP request from {addr}")
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
                    # logger.debug(f"Sent NTP response to {addr}")

                except socket.timeout:
                    continue # Go back to waiting
                except OSError as e:
                    # Handle cases like the socket being closed during shutdown
                    if e.errno == 9: # Bad file descriptor (likely closed)
                        print("NTP server socket closed.")
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
            print("NTP server process stopped.")

    def stop(self):
        """Stops the NTP server process."""
        print(f"Stopping NTP server process (PID: {self.pid})...")
        try:
            # Wait a short time for graceful exit
            self.join(timeout=2)

            # If still alive, force terminate
            if self.is_alive():
                print(f"NTP server process (PID: {self.pid}) did not terminate gracefully, forcing.")
                self.join(timeout=1) # Wait again

            # Check again and send SIGKILL if necessary
            if self.is_alive():
                logger.error(f"Failed to terminate NTP server process (PID: {self.pid}). Sending SIGKILL.")
                self.join(timeout=1)

        except ProcessLookupError:
            print(f"NTP server process (PID: {self.pid}) already terminated.")
        except Exception as e:
            logger.error(f"Error stopping NTP server process: {e}")
        finally:
            # Check process status using os.kill(pid, 0) as process object might be stale
            try:
                os.kill(self.pid, 0)
                # If os.kill doesn't raise an exception, the process is still alive
                logger.error(f"NTP server process (PID: {self.pid}) could not be stopped.")
            except OSError:
                # Process is not running
                print("NTP server process stopped successfully.")
            except AttributeError:
                print("NTP server process was already stopped or PID was invalid.")
            except Exception as e:
                logger.error(f"Error checking process status: {e}")
