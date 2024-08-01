"""Receiver, handles a port for listening for sources to send UDP packets to
   Puts received data in sink queues"""
import socket
import subprocess
from typing import List, Optional

import src.constants.constants as constants
from src.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)

class RTPReceiver():
    """Handles the main socket that listens for incoming Scream streams and sends them to sinks"""
    def __init__(self,  controller_write_fd_list: List[int]):
        """Receives UDP packets and sends them to known queue lists"""
        self.controller_write_fd_list: List[int] = controller_write_fd_list
        """List of all sink queues to forward data to"""
        self.__rtp_listener: Optional[subprocess.Popen] = None
        """RTP process"""
        if len(controller_write_fd_list) == 0:  # Will be zero if this is just a placeholder.
            return
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                constants.PACKET_SIZE * 1024 * 1024)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", constants.RTP_RECEIVER_PORT))
        self.socket_fd = self.sock.fileno()
        self.start()

    def __build_command(self) -> List[str]:
        """Builds Command to run"""
        command: List[str] = []
        command.extend(["c_utils/bin/rtp_receiver",
                        str(self.socket_fd)])
        command.extend([str(fd) for fd in self.controller_write_fd_list])
        return command

    def start(self):
        """Starts the sink mixer"""
        pass_fds: List[int] = []
        pass_fds.extend(self.controller_write_fd_list)
        pass_fds.append(self.socket_fd)
        self.__rtp_listener = subprocess.Popen(self.__build_command(),
                                        shell=False,
                                        start_new_session=True,
                                        pass_fds=pass_fds,
                                        stdin=subprocess.PIPE,
                                        )

    def stop(self):
        """Stops the sink mixer"""
        if self.__rtp_listener is not None:
            self.__rtp_listener.kill()
            self.__rtp_listener.wait()
