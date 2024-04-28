import os
import select
import threading
import socket
import io
import traceback

from typing import Optional, Tuple

from api.api_webstream import API_Webstream

import audio.mp3_header_parser as mp3_header_parser
from audio.stream_info import StreamInfo

class ffmpeg_output_thread(threading.Thread):
    """Handles listening for output from ffmpeg, extended by codec-specific classes"""
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
        fd = os.open(self._fifo_in, os.O_RDONLY | os.O_NONBLOCK)
        self._fd = open(fd, 'rb')
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
        """Reads count bytes, blocks until self.__running goes false or 'count' bytes are received."""
        dataout:bytearray = bytearray()
        """Data to return"""
        while self._running and len(dataout) < count:
            ready = select.select([self._fd], [], [], .1)
            if ready[0]:
                data: bytes = self._fd.read(count - len(dataout))
                if (data):
                    dataout.extend(data)
        return dataout


class ffmpeg_mp3_thread(ffmpeg_output_thread):
    """Handles listening for MP3 output from ffmpeg"""
    def __init__(self, fifo_in: str, sink_ip: str, webstream: Optional[API_Webstream]):
        super().__init__(fifo_in=fifo_in, sink_ip=sink_ip, name=f"[Sink {sink_ip}] MP3 Thread")

        self.__webstream: Optional[API_Webstream] = webstream
        """Holds the Webstream queue object to dump to"""
    
    def __read_header(self) -> Tuple[mp3_header_parser.MP3Header, bytes]: # type: ignore  # Can't return None, ignore warning
        """Returns a tuple of the parsed header and raw header data. Skips ID3 headers if found."""
        header_length: int = 4
        """Length of an MP3 header"""
        header: bytearray = bytearray(self._read_bytes(header_length))
        """Header bytes"""
        header_parsed: mp3_header_parser.MP3Header
        """Parsed header object"""
        max_bytes_to_search: int = 250
        """Number of bytes to search for an MP3 header after finding an ID3 header"""
        bytes_searched: int = 0
        """Number of bytes searched so far"""

        if header[0:3] == "ID3".encode():  # Found ID3 header, search for start of MP3 header
            while bytes_searched < max_bytes_to_search:
                bytes_searched = bytes_searched + 1
                bytesin: bytearray = bytearray(self._read_bytes(1))
                if bytesin[0] == 255:
                    header = bytesin + self._read_bytes(header_length - 1)
                    try:
                        header_parsed: mp3_header_parser.MP3Header = mp3_header_parser.MP3Header(header)
                        return (header_parsed, header)
                    except:
                        pass
            if bytes_searched == max_bytes_to_search:
                raise Exception(f"[Sink {self._sink_ip}] Couldn't find MP3 header after ID3 header")
        else:
            header_parsed: mp3_header_parser.MP3Header = mp3_header_parser.MP3Header(header)
            return (header_parsed, header)

    def run(self) -> None:
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
            except Exception as e:
                print(f"[Sink {self._sink_ip}] Failed processing MP3 header, MP3 streaming will fail.")
                print(traceback.format_exc())
                continue
            available_data.extend(mp3_header_raw)
            mp3_frame = self._read_bytes(mp3_header_parsed.framelength)
            available_data.extend(mp3_frame)
            available_frame_count = available_frame_count + 1
            if self.__webstream and (available_frame_count == target_frames_per_packet or len(available_data) >= target_bytes_per_packet):
                # If it has reached either target frames per packet or target bytes per packet then send the buffered data
                self.__webstream.sink_callback(self._sink_ip, available_data)
                available_frame_count = 0
                available_data = bytearray()
        print(f"[Sink {self._sink_ip}] MP3 thread exit")


class ffmpeg_pcm_thread(ffmpeg_output_thread):
    """Handles listening for PCM output from ffmpeg"""
    def __init__(self, fifo_in: str, sink_ip: str, output_info: StreamInfo):

        self.__sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        """Output socket for sink"""
        self.__output_header = output_info.header
        """Holds the header added onto packets sent to Scream receivers"""

        super().__init__(fifo_in=fifo_in, sink_ip=sink_ip, name=f"[Sink {sink_ip}] PCM Thread")

    def run(self) -> None:
        """This thread implements listening to self.fifoin and sending it out to dest_ip
           Scream Source -> Receiver -> Sink Handler -> Sources -> Pipe -> FFMPEG -> Pipe -> Python -> Scream Sink
                                                                                               ^
                                                                                          You are here                   
        """
        while self._running:
            try:
                self.__sock.sendto(self.__output_header + self._read_bytes(1152), (self._sink_ip, 4010))  # Send received data from ffmpeg to the sink
            except Exception as e:
                print(traceback.format_exc())
        print(f"[Sink {self._sink_ip}] PCM thread exit")