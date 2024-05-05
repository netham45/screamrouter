"""Receiver, handles a port for listening for sources to send UDP packets to
   Puts received data in sink queues"""
import multiprocessing
import os
import select
import socket
import sys
from ctypes import c_bool
from typing import List

import src.constants.constants as constants
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.packets import FFMpegInputQueueEntry

logger = get_logger(__name__)

class ReceiverThread(multiprocessing.Process):
    """Handles the main socket that listens for incoming Scream streams and sends them to sinks"""
    def __init__(self,  queue_list: List[multiprocessing.Queue]):
        """Receives UDP packets and sends them to known queue lists"""
        super().__init__(name="Receiver Thread")
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Main socket all sources send to"""
        self.queue_list: List[multiprocessing.Queue] = queue_list
        """List of all sink queues to forward data to"""
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        if len(queue_list) == 0:  # Will be zero if this is just a placeholder.
            self.running.value = c_bool(False)
            return
        self.start()

    def stop(self) -> None:
        """Stops the Receiver and all sinks"""
        logger.info("[Receiver] Stopping")
        was_running: bool = bool(self.running.value)
        self.running.value = c_bool(False)
        if constants.KILL_AT_CLOSE:
            try:
                self.kill()
            except AttributeError:
                pass
        if constants.WAIT_FOR_CLOSES and was_running:
            self.join()
            self.close()

    def run(self) -> None:
        """This thread listens for traffic from all sources and sends it to sinks"""
        logger.debug("[Receiver] Receiver Thread PID %s", os.getpid())
        logger.info("[Receiver] Receiver started on port %s", constants.RECEIVER_PORT)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                constants.PACKET_SIZE * 1024 * 1024)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", constants.RECEIVER_PORT))

        while self.running.value:
            if os.getppid() == 1:
                logger.warning("Exiting because parent pid is 1")
                self.stop()
                sys.exit(3)
            ready = select.select([self.sock], [], [], .1)
            if ready[0]:
                data, addr = self.sock.recvfrom(constants.PACKET_SIZE)
                for queue in self.queue_list:
                    queue.put(FFMpegInputQueueEntry(addr[0], data))
        logger.info("[Receiver] Main thread stopped")
