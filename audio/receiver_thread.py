"""Receiver, handles a port for listening for sources to send UDP packets to
   Puts received data in sink queues"""
import threading
import socket
import select

from typing import List

from screamrouter_types import PortType, SinkDescription
from audio.audio_controller import AudioController
from logger import get_logger

logger = get_logger(__name__)

class ReceiverThread(threading.Thread):
    """Handles the main socket that listens for incoming Scream streams and sends them to sinks"""
    def __init__(self, port: PortType):
        """Takes the UDP port number to listen on"""
        super().__init__(name="Main Receiver Thread")
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Main socket all sources send to"""
        self.audio_controllers: List[AudioController] = []
        """List of all sinks to forward data to"""
        self.running: bool = True
        """Rather the Recevier is running, when set to false the receiver ends"""
        self.port: PortType = port
        """UDP port to listen on"""
        self.start()

    def register_audio_controller(self, audio_controller: AudioController) -> None:
        """Add a sink"""
        for _audio_controller in self.audio_controllers:
            if audio_controller.sink_info.name == _audio_controller.sink_info.name:
                raise NameError(f"Duplicate sink controller {_audio_controller.sink_info.name}")
        self.audio_controllers.append(audio_controller)

    def unregister_audio_controller_by_sink(self, sink: SinkDescription) -> None:
        """Remove a sink"""
        for index, audio_controller in enumerate(self.audio_controllers):
            if audio_controller.sink_info.name == sink.name:
                self.audio_controllers.pop(index)
                audio_controller.stop()
                audio_controller.wait_for_threads_to_stop()

    def stop(self) -> None:
        """Stops the Receiver and all sinks"""
        logger.info("[Recevier] Stopping")
        self.running = False
        self.sock.close()
        self.join()

    def __check_source_packet(self, tag: str, data: bytes) -> bool:
        """Verifies a packet is the right length"""
        if len(data) != 1157:
            logger.warning("[Source:%s] Got bad packet length %i != 1157 from source",
                        tag,
                        len(data))
            return False
        return True

    def add_packet_to_queue(self, tag: str, data: bytes):
        """Adds a packet to all sinks' queues"""
        if self.__check_source_packet(tag, data):
            for sink in self.audio_controllers:
                sink.add_packet_to_queue(tag, data)

    def notify_url_done_playing(self, tag: str):
        """Notifies all registered sinks that a URL is done playing"""
        for sink in self.audio_controllers:
            sink.url_playback_done_callback(tag)

    def run(self) -> None:
        """This thread listens for traffic from all sources and sends it to sinks"""
        if self.running:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1157 * 65535)
            self.sock.bind(("", self.port))
        else:
            return

        recvbuf = bytearray(1157)
        while self.running:
            ready = select.select([self.sock], [], [], .005)
            if ready[0]:
                try:
                    recvbuf, addr = self.sock.recvfrom(1157)  # 5 bytes header + 1152 bytes pcm
                    if self.__check_source_packet(addr[0], recvbuf):
                        for sink in self.audio_controllers:
                            sink.add_packet_to_queue(addr[0], recvbuf)
                except OSError:
                    logger.warning("[Receiver] Failed to read from incoming sock, exiting")
                    break
        logger.info("[Receiver] Main thread ending sinks")
        for sink in self.audio_controllers:
            logger.info("[Receiver] Stopping sink %s", sink.sink_info.ip)
            sink.stop()
        for sink in self.audio_controllers:
            logger.debug("[Receiver] Waiting for sink %s to stop", sink.sink_info.ip)
            sink.wait_for_threads_to_stop()
        self.sock.close()

        logger.info("[Receiver] Main thread stopped")
