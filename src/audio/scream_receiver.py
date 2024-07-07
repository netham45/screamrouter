"""Receiver, handles a port for listening for sources to send UDP packets to
   Puts received data in sink queues"""
import multiprocessing
import os
import select
import socket
from ctypes import c_bool
from subprocess import TimeoutExpired
from typing import List

import src.constants.constants as constants
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.utils.utils import close_all_pipes, set_process_name

logger = get_logger(__name__)

class ScreamReceiver(multiprocessing.Process):
    """Handles the main socket that listens for incoming Scream streams and sends them to sinks"""
    def __init__(self,  controller_write_fd_list: List[int]):
        """Receives UDP packets and sends them to known queue lists"""
        super().__init__(name="Scream Receiver Thread")
        self.sock: socket.socket
        """Main socket all sources send to"""
        self.controller_write_fd_list: List[int] = controller_write_fd_list
        """List of all sink queues to forward data to"""
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.known_ips = multiprocessing.Manager().list()
        if len(controller_write_fd_list) == 0:  # Will be zero if this is just a placeholder.
            self.running.value = c_bool(False) # type: ignore
            return
        self.start()

    def stop(self) -> None:
        """Stops the Receiver and all sinks"""
        logger.info("[Scream Receiver] Stopping")
        was_running: bool = bool(self.running.value)
        self.running.value = c_bool(False) # type: ignore
        if constants.KILL_AT_CLOSE:
            try:
                self.kill()
            except AttributeError:
                pass
        if constants.WAIT_FOR_CLOSES and was_running:
            try:
                self.join(5)
            except TimeoutExpired:
                logger.warning("Receiver failed to close")

    def run(self) -> None:
        """This thread listens for traffic from all sources and sends it to sinks"""
        set_process_name("ScreamReceiver", "Scream Receiver Thread")
        logger.debug("[Scream Receiver] Receiver Thread PID %s", os.getpid())
        logger.info("[Scream Receiver] Receiver started on port %s", constants.SCREAM_RECEIVER_PORT)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                constants.PACKET_SIZE * 1024 * 1024)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", constants.SCREAM_RECEIVER_PORT))

        while self.running.value:
            try:
                ready = select.select([self.sock], [], [], .3)
                if ready[0]:
                    data, addr = self.sock.recvfrom(constants.PACKET_SIZE)
                    addrlen = len(addr[0])
                    if addr[0] not in self.known_ips:
                        self.known_ips.append(addr[0])
                    for controller_write_fd in self.controller_write_fd_list:
                        os.write(controller_write_fd,
                            bytes(addr[0].encode("ascii") +
                                bytes([0] * (constants.TAG_MAX_LENGTH - addrlen)) +
                                data))
            except ValueError:
                pass
        logger.info("[Scream Receiver] Main thread stopped")
        close_all_pipes()
