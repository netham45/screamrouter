"""Receiver, handles a port for listening for sources to send UDP packets to
   Puts received data in sink queues"""
import threading
import socket
from subprocess import TimeoutExpired
import time
from typing import List, Optional, Tuple

from src.audio.audio_controller import AudioController
from src.screamrouter_types.annotations import IPAddressType
import src.constants.constants as constants
from src.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)

class TCPManager(threading.Thread):
    """Handles the TCP Sender socket"""
    def __init__(self,  audio_controllers: List[AudioController]):
        """Listens for incoming connections on TCP and passes them to listeners"""
        super().__init__(name="TCP Connection Manager")
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        """Main socket all sources send to"""
        self.audio_controllers: List[AudioController] = audio_controllers
        """List of all sink queues to forward data to"""
        self.running: bool = True
        """Multiprocessing-passed flag to determine if the thread is running"""
        bound: bool = False
        while not bound:
            try:
                self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                self.sock.bind(("0.0.0.0", 4010))
                bound = True
            except OSError:
                logger.warning("Binding to port %s failed, retrying in .5 seconds", 4010)
                time.sleep(.5)
        self.known_connections: List[Tuple[IPAddressType, socket.socket]] = []
        self.wants_reload: bool = False
        """Set to true when TCP manager wants a config reload"""
        self.sock.listen(12321312)
        self.start()

    def stop(self) -> None:
        """Stops the Sender"""
        logger.info("[TCP Manager] Stopping")
        was_running: bool = self.running
        self.running = False
        #for connection in self.known_connections:
            #connection[1].close()
        self.sock.close()
        if constants.WAIT_FOR_CLOSES and was_running:
            try:
                self.join(5)
            except TimeoutExpired:
                logger.warning("TCP Manager failed to close")

    def replace_mixers(self, audio_controllers: List[AudioController]) -> None:
        """Replace mixers with a new set"""
        self.audio_controllers = audio_controllers

    def get_fd(self, sink_ip: Optional[IPAddressType]) -> Optional[int]:
        """Returns the FD for a sink IP, None if not found"""
        if sink_ip is None:
            return None
        for connection in reversed(self.known_connections):
            if str(connection[0]) == str(sink_ip):
                return connection[1]
        return None

    def run(self) -> None:
        self.sock.set_inheritable(True)
        while self.running:
            client, address = self.sock.accept()
            logger.info("[TCP Manager] New connection from %s fd: %s", address[0], client.fileno())
            self.known_connections.append((address[0], client.fileno()))
            client.set_inheritable(True)
            for audio_controller in self.audio_controllers:
                logger.info("Comparing IP %s to %s",
                            audio_controller.pcm_thread.sink_info.ip, address[0])
                if str(audio_controller.pcm_thread.sink_info.ip) == str(address[0]):
                    logger.info("[TCP Manager] Wrote FD to mixer %s, fd %s",
                                address[0], client.fileno())
                    time.sleep(4)
                    audio_controller.restart_mixer(client.fileno())
                    self.wants_reload = True
            client.detach()
