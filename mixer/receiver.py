import threading
import socket
import select

import traceback

from typing import List

from mixer.sink import Sink

LOCALPORT=16401

class Receiver(threading.Thread):
    """Handles the main socket that listens for incoming Scream streams and sends them to the appropriate sinks"""
    def __init__(self):
        """Takes no parameters"""
        super().__init__()
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Main socket all sources send to"""
        self.sinks: List[Sink] = []
        """List of all sinks to forward data to"""
        self.running: bool = True
        """Rather the Recevier is running, when set to false the receiver ends"""
        self.start()

    def register_sink(self, sink: Sink) -> None:
        """Add a sink"""
        self.sinks.append(sink)

    def stop(self) -> None:
        """Stops the Receiver and all sinks"""
        self.running = False
        self.sock.close()
    
    def __check_source_packet(self, source_ip: str, data: bytes) -> bool:
        """Verifies a packet is the right length"""
        if len(data) != 1157:
            print(f"[Source {source_ip}] Got bad packet length {len(data)} != 1157 from source")
            return False
        return True

    def run(self) -> None:
        """This thread listens for traffic from all sources and sends it to sinks
        Scream Source -> Receiver -> Sink Handler -> Sources -> Pipe -> FFMPEG -> Pipe -> Python -> Scream Sink
                            ^
                       You are here                   
        """
        self.sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,4096)
        self.sock.bind(("", LOCALPORT))

        recvbuf = bytearray(1157)
        while self.running:
            ready = select.select([self.sock], [], [], .2)  # If the socket is dead for more than .2 seconds kill ffmpeg
            if ready[0]:
                try:
                    recvbuf, addr = self.sock.recvfrom(1157)  # 5 bytes header + 1152 bytes pcm
                    if self.__check_source_packet(addr[0], recvbuf):
                        for sink in self.sinks:  # Send the data to each recevier, they'll decide if they need to deal with it
                            sink.process_packet_from_receiver(addr[0], recvbuf)
                except Exception as e:
                    continue
        print(f"[Receiver] Main thread ending sinks")
        for sink in self.sinks:
            print(f"[Receiver] Stopping sink {sink._sink_ip}")
            sink.stop()

        print(f"[Receiver] Main thread ending!")
