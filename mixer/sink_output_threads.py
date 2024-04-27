import os
import select
import threading
import socket
import io
import traceback

from typing import Optional, Tuple

from api.api_webstream import API_Webstream

import mixer.mp3_header_parser as mp3_header_parser

class sink_output_thread(threading.Thread):
    def __init__(self, fifo_in: str, sink_ip: str, name: str):
        super().__init__(name = name)
        self._fifo_in: str = fifo_in
        """Holds the ffmpeg output pipe name this sink will use as a source"""
        self._sink_ip: str = sink_ip
        """Holds the sink IP for the web api to filter based on"""
        self._running: bool = True
        """Rather this thread is running"""
        self._fd: io.BufferedReader
        """File handle"""
        self._make_ffmpeg_to_screamrouter_pipe()  # Make python -> ffmpeg fifo fifo
        self.start()

    def _make_ffmpeg_to_screamrouter_pipe(self) -> bool:
        """Makes fifo out for python sending to ffmpeg"""
        try:
            try:
                os.remove(self._fifo_in)
            except:
                pass
            os.mkfifo(self._fifo_in)
            return True
        except:
            print(traceback.format_exc())
            return False
        
    def stop(self) -> None:
        """Stop"""
        self._running = False
        try:
            self._fd.close()
        except:
            pass
        try:
            os.remove(self._fifo_in)
        except:
            pass

    def _read_bytes(self, count: int) -> bytes:
        """Reads count bytes, doesn't return unless self.__running goes false or bytes count is reached."""
        dataout = bytearray()
        while self._running and len(dataout) < count:
            ready = select.select([self._fd], [], [], .1)
            if ready[0]:
                data: bytes = self._fd.read(count - len(dataout))
                if (data):
                    dataout.extend(data)
        return dataout


class sink_mp3_thread(sink_output_thread):
    """Handles listening for MP3 output from ffmpeg"""
    def __init__(self, fifo_in: str, sink_ip: str, webstream: Optional[API_Webstream]):
        super().__init__(fifo_in=fifo_in, sink_ip=sink_ip, name=f"[Sink {sink_ip}] MP3 Thread")

        self.__webstream: Optional[API_Webstream] = webstream
        """Holds the Webstream queue object to dump to"""
    
    def __read_header(self) -> Tuple[mp3_header_parser.MP3Header, bytes]:
        """Returns a tuple of the parsed header and raw header data. Skips ID3 headers if found."""
        header_length: int = 4  # MP3 header length is 4 bytes
        ID3_length: int = 45  #  TODO: Assuming this is 45 bytes is bad. Seems to be reliable for ffmpeg but won't be for other MP3 sources.
        header = self._read_bytes(header_length)
        if header[0:3] == "ID3".encode():
            self._read_bytes(ID3_length - header_length)  # Discard ID3 data
            header = self._read_bytes(header_length)
        header_parsed: mp3_header_parser.MP3Header = mp3_header_parser.MP3Header(header)
        return (header_parsed, header)

    def run(self) -> None:
        fd = os.open(self._fifo_in, os.O_RDONLY | os.O_NONBLOCK)
        self._fd = open(fd, 'rb')
        target_frames_per_packet: int = 1
        """Send data to the web handler when it gets this many frames buffered up"""
        target_bytes_per_packet: int = 1500
        """Send data to the web handler when it gets this many bytes buffered up"""
        available_data = bytearray()
        """Holds the available frames"""
        available_frame_count: int = 0
        """Holds the number of available frames"""
        while self._running:
            mp3_header_parsed: mp3_header_parser.MP3Header
            """Holds the parsed header object"""
            mp3_header_raw: bytes
            """Holds the raw header bytes"""
            mp3_frame: bytes
            """Holds the currently processing MP3 frame"""
            try:
                mp3_header_parsed, mp3_header_raw = self.__read_header()
            except:
                continue
            available_data.extend(mp3_header_raw)
            mp3_frame = self._read_bytes(mp3_header_parsed.framelength)
            available_data.extend(mp3_frame)
            available_frame_count = available_frame_count + 1
            if self.__webstream and (available_frame_count == target_frames_per_packet or len(available_data) >= target_bytes_per_packet):
                # If we have reached either target frames per packet or target bytes per packet then send the buffered data
                self.__webstream.sink_callback(self._sink_ip, available_data)
                available_frame_count = 0
                available_data = bytearray()
        print(f"[Sink {self._sink_ip}] MP3 thread exit")

class sink_pcm_thread(sink_output_thread):
    """Handles listening for PCM output from ffmpeg"""
    def __init__(self, fifo_in: str, sink_ip: str):
        super().__init__(fifo_in=fifo_in, sink_ip=sink_ip, name=f"[Sink {sink_ip}] PCM Thread")

        self.__output_header = bytes([0x01, 0x20, 0x02, 0x03, 0x00])  # 48khz, 32-bit, stereo"""
        """Holds the header added onto packets sent to Scream receivers"""  # TODO: Dynamically generate this header based on config
        self.__sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Output socket for sink"""

    def run(self) -> None:
        """This thread implements listening to self.fifoin and sending it out to dest_ip
           Scream Source -> Receiver -> Sink Handler -> Sources -> Pipe -> FFMPEG -> Pipe -> Python -> Scream Sink
                                                                                               ^
                                                                                          You are here                   
        """
        fd = os.open(self._fifo_in, os.O_RDONLY | os.O_NONBLOCK)
        self._fd = open(fd, 'rb')
        
        while self._running:
            data = bytearray()
            try:
                data.extend(self._read_bytes(1152))  # 1152 for Scream compatibility
                sendbuf = self.__output_header + data  # Add the header to the data
                self.__sock.sendto(sendbuf, (self._sink_ip, 4010))  # Send it to the sink
            except Exception as e:
                print(traceback.format_exc())
        print(f"[Sink {self._sink_ip}] PCM thread exit")