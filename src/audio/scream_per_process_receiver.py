"""Receiver, handles a port for listening for sources to send UDP packets to
   Puts received data in sink queues"""
import os
import select
import socket
import subprocess
import threading
import time
from typing import List, Optional

import src.constants.constants as constants
from src.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)

class ScreamPerProcessReceiver():
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
        self.known_sources: list[str] = []
        """List of known sources"""
        self.running: bool = True
        """Whether or not the source is currently running"""
        self.logging_thread = threading.Thread(target=self.__log_output)
        if len(controller_write_fd_list) == 0:  # Will be zero if this is just a placeholder.
            return
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                constants.PER_PROCESS_PACKET_SIZE * 1024 * 1024)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", constants.SCREAM_PER_PROCESS_RECEIVER_PORT))
        self.socket_fd = self.sock.fileno()
        self.start()

        """Thread to log output from process"""
        self.logging_thread.start()

    def __log_output(self):
        try:
            logger.debug("[Scream Per-Process Receiver] Logging Start")
            while self.running:
                if self.__scream_listener is not None:
                    logger.debug("[Scream Per-Process Receiver] Logging In")
                    data = self.__scream_listener.stdout.read(1024)
                    if not data:
                        break
                    logger.info("[Scream Per-Process Receiver] %s", data)
                else:
                    time.sleep(1)
        except OSError as e:
            logger.error("Error in logging thread for Scream Per-Process Receiver %s", e)

    def __build_command(self) -> List[str]:
        """Builds Command to run"""
        command: List[str] = []
        command.extend([#"/usr/bin/valgrind", "--tool=callgrind", "--trace-children=yes", "--collect-jumps=yes", "--dump-instr=yes",
                        "c_utils/bin/scream_per_process_receiver",
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
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT,
                                        text=True
                                        )

    def check_known_sources(self):
        """Checks for new Process:IP addresses to consider connected"""

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
            new_sources = data.split('\n')

            for new_source in new_sources:
                new_source = new_source.strip()
                if not new_source:
                    continue  # Skip empty lines
                    # Convert the IP string to IPAddressType (assuming it's a valid IP)
                try:
                    if new_source not in self.known_sources:
                        self.known_sources.append(new_source)
                        logger.info("New Process:IP address connected: %s", new_source)
                except ValueError:
                    logger.warning("Received invalid Process:IP address: %s", new_source)

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
        if self.logging_thread is not None and self.logging_thread.is_alive():
            self.logging_thread.join()
