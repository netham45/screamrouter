"""Receiver, handles a port for listening for sources to send UDP packets to
   Puts received data in sink queues"""
import os
import socket
import struct
import select
import subprocess
from typing import List, Optional

from pydantic import IPvAnyAddress

from src.screamrouter_types.annotations import IPAddressType
import src.constants.constants as constants
from src.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)

class MulticastScreamReceiver():
    """Handles the main socket that listens for incoming Scream streams and sends them to sinks"""
    def __init__(self,  controller_write_fd_list: List[int]):
        """Receives UDP packets and sends them to known queue lists"""
        self.controller_write_fd_list: List[int] = controller_write_fd_list
        """List of all sink queues to forward data to"""
        self.__scream_listener: Optional[subprocess.Popen] = None
        """Scream process"""
        self.data_output_fd: int
        """Listened to for new IP addresses to consider connected"""
        self.data_input_fd: int
        """Passed to the listener for it to send data back to Python"""
        self.data_output_fd, self.data_input_fd = os.pipe()
        self.known_ips: list[IPAddressType] = []
        if len(controller_write_fd_list) == 0:  # Will be zero if this is just a placeholder.
            return
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                constants.PACKET_SIZE * 1024 * 1024)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        # Set up multicast
        self.multicast_group = '239.255.77.77'
        self.server_address = ('', 4010)  # Listen on all available interfaces

        # Bind to the server address
        self.sock.bind(self.server_address)

        # Tell the operating system to add the socket to the multicast group
        group = socket.inet_aton(self.multicast_group)
        mreq = struct.pack('4sL', group, socket.INADDR_ANY)
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

        self.socket_fd = self.sock.fileno()
        self.start()

    def __build_command(self) -> List[str]:
        """Builds Command to run"""
        command: List[str] = []
        command.extend(["c_utils/bin/scream_receiver",
                        str(self.socket_fd),
                        str(self.data_input_fd)])
        command.extend([str(fd) for fd in self.controller_write_fd_list])
        return command

    def start(self):
        """Starts the sink mixer"""
        pass_fds: List[int] = []
        pass_fds.extend(self.controller_write_fd_list)
        pass_fds.append(self.data_input_fd)
        pass_fds.append(self.socket_fd)
        self.__scream_listener = subprocess.Popen(self.__build_command(),
                                        shell=False,
                                        start_new_session=True,
                                        pass_fds=pass_fds,
                                        stdin=subprocess.PIPE,
                                        )

    def check_known_ips(self):
        """Checks for new IP addresses to consider connected"""

        # Use select to check if there's data available on self.data_output_fd
        ready_to_read, _, _ = select.select([self.data_output_fd], [], [], 0)

        # If self.data_output_fd is not ready to be read, return immediately
        if self.data_output_fd not in ready_to_read:
            return

        # If there's data available, proceed with reading and processing
        try:
            # Read from the data input fd
            data = os.read(self.data_output_fd, 1024).decode().strip()

            # Split the data into lines, each containing an IP
            new_ips = data.split('\n')

            for new_ip in new_ips:
                new_ip = new_ip.strip()
                if not new_ip:
                    continue  # Skip empty lines
                    # Convert the IP string to IPAddressType (assuming it's a valid IP)
                try:
                    ip_address: IPAddressType = IPvAnyAddress(new_ip) # type: ignore
                    # Add the IP to known_ips if not already present
                    if ip_address not in self.known_ips:
                        self.known_ips.append(ip_address)
                        logger.info("New IP address connected: %s", ip_address)
                except ValueError:
                    logger.warning("Received invalid IP address: %s", new_ip)

        except OSError:
            # No more data to read or error occurred
            pass

    def stop(self):
        """Stops the sink mixer"""
        if self.__scream_listener is not None:
            self.__scream_listener.kill()
            self.__scream_listener.wait()
        os.close(self.data_input_fd)
        os.close(self.data_output_fd)
