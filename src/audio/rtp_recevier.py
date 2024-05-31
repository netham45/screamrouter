"""Receiver, handles a port for listening for sources to send UDP packets to
   Puts received data in sink queues"""
import multiprocessing
import os
import select
import socket
from ctypes import c_bool
from subprocess import TimeoutExpired
from typing import List
import rtp
from rtp.payloadType import PayloadType

from src.audio.scream_header_parser import create_stream_info
import src.constants.constants as constants
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.utils.utils import close_all_pipes, set_process_name

logger = get_logger(__name__)
RTP = rtp.RTP()

class RTPReceiver(multiprocessing.Process):
    """Handles the main socket that listens for incoming Scream streams and sends them to sinks"""
    def __init__(self,  controller_write_fd_list: List[int]):
        """Receives UDP packets and sends them to known queue lists"""
        super().__init__(name="RTP Receiver Thread")
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Main socket all sources send to"""
        self.controller_write_fd_list: List[int] = controller_write_fd_list
        """List of all sink queues to forward data to"""
        self.running = multiprocessing.Value(c_bool, True)
        """Multiprocessing-passed flag to determine if the thread is running"""
        self.known_ips = multiprocessing.Manager().list()
        if len(controller_write_fd_list) == 0:  # Will be zero if this is just a placeholder.
            self.running.value = c_bool(False)
            return
        self.start()

    def stop(self) -> None:
        """Stops the Receiver and all sinks"""
        logger.info("[RTP Receiver] Stopping")
        was_running: bool = bool(self.running.value)
        self.running.value = c_bool(False)
        if constants.KILL_AT_CLOSE:
            try:
                self.kill()
            except AttributeError:
                pass
        if constants.WAIT_FOR_CLOSES and was_running:
            try:
                self.join(5)
            except TimeoutExpired:
                logger.warning("RTP Receiver failed to close")

    def run(self) -> None:
        """This thread listens for traffic from all sources and sends it to sinks"""
        set_process_name("RTPRecvr", "RTP Receiver Thread")
        logger.debug("[RTP Receiver] Receiver Thread PID %s", os.getpid())
        logger.info("[RTP Receiver] Receiver started on port %s", constants.RTP_RECEIVER_PORT)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                constants.PACKET_SIZE * 1024 * 1024)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", constants.RTP_RECEIVER_PORT))
        mono_header = create_stream_info(16, 48000, 1, "mono")
        stereo_header = create_stream_info(16, 48000, 2, "stereo")

        while self.running.value:
            ready = select.select([self.sock], [], [], .3)
            if ready[0]:
                data, addr = self.sock.recvfrom(constants.PACKET_SIZE + 500)
                rtp_packet = RTP.fromBytearray(bytearray(data))
                if not rtp_packet.payloadType in [PayloadType.L16_1chan,
                                                  PayloadType.L16_2chan,
                                                  PayloadType.DYNAMIC_127]:
                    logger.warning("Can only decode 16-bit LPCM, unsupported type %s from %s:%s",
                                   rtp_packet.payloadType, addr[0], addr[1])
                    continue
                if addr[0] not in self.known_ips:
                    self.known_ips.append(addr[0])
                padded_tag: bytes
                padded_tag = bytes(addr[0].encode("ascii"))
                padded_tag += bytes([0] * (constants.TAG_MAX_LENGTH - len(addr[0])))
                header: bytes = mono_header.header
                if rtp_packet.payloadType in [PayloadType.L16_2chan,
                                              PayloadType.DYNAMIC_127]:
                    header = stereo_header.header
                for controller_write_fd in self.controller_write_fd_list:
                    os.write(controller_write_fd, padded_tag + header + rtp_packet.payload)

        logger.info("[RTP Receiver] Main thread stopped")
        close_all_pipes()
