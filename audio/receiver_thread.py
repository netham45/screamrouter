"""Receiver, handles a port for listening for sources to send UDP packets to
   Puts received data in sink queues"""
import multiprocessing
import os
import socket
import select

from typing import List

import constants
from logger import get_logger
from screamrouter_types.packets import FFMpegInputQueueEntry

logger = get_logger(__name__)

class ReceiverThread(multiprocessing.Process):
    """Handles the main socket that listens for incoming Scream streams and sends them to sinks"""
    def __init__(self,  queue_list: List[multiprocessing.Queue]):
        """Takes the UDP port number to listen on"""
        super().__init__(name="Receiver Thread")
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Main socket all sources send to"""
        self.queue_list: List[multiprocessing.Queue] = queue_list
        """List of all sink queues to forward data to"""
        self.start()

    def stop(self) -> None:
        """Stops the Receiver and all sinks"""
        logger.info("[Receiver] Stopping")
        self.sock.close()
        self.kill()
        self.terminate()
        self.join()

    def run(self) -> None:
        """This thread listens for traffic from all sources and sends it to sinks"""
        logger.debug("[Receiver] Receiver Thread PID %s", os.getpid())
        logger.info("[Receiver] Receiver started on port %s", constants.RECEIVER_PORT)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                constants.PACKET_SIZE * 1024 * 1024)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", constants.RECEIVER_PORT))

        while True:
            ready = select.select([self.sock], [], [], .005)
            if ready[0]:
                try:
                    data, addr = self.sock.recvfrom(constants.PACKET_SIZE)
                    for queue in self.queue_list:
                        queue.put(FFMpegInputQueueEntry(addr[0], data))
                except OSError:
                    logger.warning("[Receiver] Failed to read from incoming sock, exiting")
                    break
        logger.info("[Receiver] Main thread stopped")
