import socket
import threading
import os
import time
import subprocess
import tempfile
import select
from typing import List
import traceback
import numpy
import io

class ScreamStreamInfo():
    """Parses Scream headers to get sample rate, bit depth, and channels"""
    def __init__(self, scream_header):
        """Parses the first five bytes of a Scream header to get the stream attributes"""
        sample_rate_bits = numpy.unpackbits(numpy.array([scream_header[0]], dtype=numpy.uint8), bitorder='little')  # Unpack the first byte into 8 bits
        sample_rate_base = 44100 if sample_rate_bits[7] == 1 else 48000  # If the uppermost bit is set then the base is 44100, if it's not set the base is 48000
        sample_rate_bits = numpy.delete(sample_rate_bits, 7)  # Remove the uppermost bit
        sample_rate_multiplier = numpy.packbits(sample_rate_bits,bitorder='little')[0]  # Convert it back into a number without the top bit, this is the multiplier to multiply the base by
        if sample_rate_multiplier < 1:
            sample_rate_multiplier = 1
        self.sample_rate: int = sample_rate_base * sample_rate_multiplier
        self.bit_depth: int = scream_header[1]  # One byte for bit depth
        self.channels: int = scream_header[2]  # One byte for channel count
        self.map: bytearray = scream_header[3:]  # Two bytes for WAVEFORMATEXTENSIBLE

    def __eq__(self, other):
        """Returns if two ScreamStreamInfos equal, minus the WAVEFORMATEXTENSIBLE part"""
        return (self.sample_rate == other.sample_rate) and (self.bit_depth == other.bit_depth) and (self.channels == other.channels)


class Source():
    """Stores the status for a single Source to a single Sink"""
    def __init__(self, ip: str, fifo_file_name: str):
        """Initializes a new Source object"""
        self._ip: str = ip
        self.__open: bool = False
        self.__last_data_time: int = 0
        self._stream_attributes: ScreamStreamInfo = ScreamStreamInfo([0,0,0,0,0])
        self._fifo_file_name: str = fifo_file_name
        self.__fifo_file_handle: io.IOBase

    def check_attributes(self, stream_attributes: ScreamStreamInfo) -> bool:
        """Returns True if the stream attributes are the same, False if they're different."""
        return stream_attributes == self._stream_attributes

    def set_attributes(self, stream_attributes: ScreamStreamInfo) -> None:
        """Sets stream attributes"""
        self._stream_attributes = stream_attributes

    def is_active(self) -> bool:
        """Returns if the source has been active in the last 200ms"""
        now: int = time.time() * 1000
        if now - self.__last_data_time > 200:
            return False
        return True

    def update_activity(self) -> None:
        """Sets the source last active time"""
        now: int = time.time() * 1000
        self.__last_data_time = now

    def is_open(self) -> bool:
        """Returns if the source is open"""
        return self.__open

    def open(self) -> None:
        """Opens the source"""
        if not self.__open:
            try:
                try:
                    os.remove(self._fifo_file_name)
                except:
                    pass
                os.mkfifo(self._fifo_file_name)
                fd = os.open(self._fifo_file_name, os.O_RDWR | os.O_NONBLOCK)
                self.__fifo_file_handle = os.fdopen(fd, 'wb', 0)
            except:
                print(traceback.format_exc())
            self.__open = True
            self.update_activity()

    def close(self) -> None:
        """Closes the source"""
        try:
            self.__fifo_file_handle.close()  # Close and remove the fifo handle so ffmpeg will stop trying to listen for it
            os.remove(self._fifo_file_name)  # This avoids needing an ffmpeg restart when an incoming stream is closed for a multi-stream ffmpeg mixer
        except:
            pass
        self.__open = False

    def stop(self) -> None:
        """Fully stops and closes the source, closes fifo handles"""
        self.__open = False
        try:
            self.__fifo_file_handle.close()
        except:
            print(traceback.format_exc())

    def write(self, data: bytearray) -> None:
        """Writes data to this source's FIFO"""
        self.__fifo_file_handle.write(data)
        self.update_activity()


class Sink(threading.Thread):
    """Handles ffmpeg, keeps a list of it's own sources, sends passed data to the appropriate pipe"""
    def __init__(self, receiver, dest_ip: str, source_ips: List[str]):
        """This gets data from multiple sources from a master, mixes them, and sends them back out to destip"""
        super().__init__()
        self._dest_ip: str = dest_ip
        self.__sources: List[Source] = []
        self.__temp_path: str = tempfile.gettempdir() + f"/scream-{self._dest_ip}-"  # Per-sink temp path
        self.__fifo_in: str = self.__temp_path + "in"  # Input file from ffmpeg
        self.__running: bool = True
        self.__ffmpeg: os.popen = None
        self.__sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        self.__make_in_fifo()  # Make python -> ffmpeg fifo fifo

        for source_ip in source_ips:  # Initialize dicts based off of source ips
            self.__sources.append(Source(source_ip, self.__temp_path + source_ip))

        self.start()  # Start our listener thread

    def get_fifo_in(self) -> str:
        """Test function to return fifoin value"""
        return self.__fifo_in

    def get_sources(self) -> List[Source]:
        """Test function to return sources"""
        return self.__sources

    def __get_open_sources(self) -> List[Source]:
        """Build a list of active IPs, exclude ones that aren't open"""
        active_sources: List[Source] = []
        for source in self.__sources:
            if source.is_open():
                active_sources.append(source)
        return active_sources

    def __get_ffmpeg_inputs(self, active_sources: List[Source]) -> List[str]:
        """Add an input for each source"""
        ffmpeg_command: List[str] = []
        for source in active_sources:
            bit_depth = source._stream_attributes.bit_depth
            sample_rate = source._stream_attributes.sample_rate
            channels = source._stream_attributes.channels
            ip = source._ip
            file_name = source._fifo_file_name
            ffmpeg_command.extend(['-thread_queue_size', '64',
                                   '-f', f's{bit_depth}le',
                                   '-ac', f'{channels}',
                                   '-ar', f'{sample_rate}',
                                   '-i', f'{file_name}'])
        return ffmpeg_command

    def __get_ffmpeg_filters(self, active_sources: List[Source]) -> List[str]:
        """Build complex filter"""
        ffmpeg_command_parts: List[str] = []
        full_filter_string = ""
        amix_inputs = ""
        per_input_filter = "asetpts='(RTCTIME - RTCSTART) / (TB * 1000000)',aresample=async=5000:flags=+res:resampler=soxr"

        for i in range(0, len(active_sources)):  # For each source IP add an input to aresample async, and append it to an input variable for amix
            full_filter_string = full_filter_string + f"[{i}]{per_input_filter}[a{i}],"
            amix_inputs = amix_inputs + f"[a{i}]"  # amix input
        if len(active_sources) > 1:
            ffmpeg_command_parts.extend(['-filter_complex', full_filter_string + amix_inputs + "amix=normalize=0"])
        else:
            ffmpeg_command_parts.extend(['-filter_complex', full_filter_string + amix_inputs + "aresample[0]"])
        return ffmpeg_command_parts

    def __get_ffmpeg_output(self) -> List[str]:
        """Returns the ffmpeg output"""
        # TODO: Add output bitdepth/channels/sample rate to yaml
        ffmpeg_command_parts: List[str] = []
        ffmpeg_command_parts.extend(['-y', '-f', 's32le', '-ac', '2', '-ar', '48000', f"file:{self.__fifo_in}"])  # ffmpeg output
        return ffmpeg_command_parts

    def __get_ffmpeg_command(self) -> List[str]:
        """Builds the ffmpeg command"""
        active_sources: List[Source] = self.__get_open_sources()
        ffmpeg_command_parts: List[str] = ['ffmpeg', '-hide_banner']  # Build ffmpeg command
        ffmpeg_command_parts.extend(self.__get_ffmpeg_inputs(active_sources))
        ffmpeg_command_parts.extend(self.__get_ffmpeg_filters(active_sources))
        ffmpeg_command_parts.extend(self.__get_ffmpeg_output())  # ffmpeg output
        return ffmpeg_command_parts

    def __make_in_fifo(self) -> bool:
        """Makes fifo in for ffmpeg to send back to python"""
        try:
            try:
                os.remove(self.__fifo_in)
            except:
                pass
            os.mkfifo(self.__fifo_in)
            return True
        except:
            print(traceback.format_exc())
            return False

    def __reset_ffmpeg(self) -> None:
        """Opens the ffmpeg instance"""
        print("(Re)starting ffmpeg for sink " + self._dest_ip)
        if self.__ffmpeg:
            try:
                self.__ffmpeg.kill()
                self.__ffmpeg.wait()
            except:
                print(traceback.format_exc())
                print(f"Failed to close ffmpeg for {self.__temp_path}!")
        self.__ffmpeg = subprocess.Popen(self.__get_ffmpeg_command(), shell=False, stdout=subprocess.PIPE, stdin=subprocess.PIPE)

    def __check_for_inactive_sources(self) -> None:
        """Looks for old pipes that are open and closes them"""
        if len(self.__get_open_sources()) < 2:  # Don't close the last pipe
            return

        for source in self.__sources:
            if not source.is_active() and source.is_open():
                print(f"[{self._dest_ip}] Source {source._ip} closing (Timeout)")
                source.close()

    def __update_pipe_attributes_open_pipe(self, source: Source, header: bytearray) -> None:
        """Verifies the target pipe header matches what we have, updates it if not. Also opens the pipe."""
        parsed_scream_header = ScreamStreamInfo(header)
        if not source.check_attributes(parsed_scream_header):
            print(f"Source {source._ip} had a stream property change. Was: {source._stream_attributes.bit_depth}-bit at {source._stream_attributes.sample_rate}kHz, is now {parsed_scream_header.bit_depth}-bit at {parsed_scream_header.sample_rate}kHz.")
            source.set_attributes(parsed_scream_header)
            print(f"[{self._dest_ip}] Source {source._ip} closing (Attribute Change)")
            source.close()
        if not source.is_open():
            print(f"[{self._dest_ip}] Source {source._ip} opening")
            source.open()
            self.__reset_ffmpeg()

    def __check_packet(self, source: Source, data: bytearray) -> bool:
        """Verifies a packet is the right length"""
        if len(data) != 1157:
            print(f"Got bad packet length {len(data)} from source {source._ip}")
            return False
        return True
    
    def __get_source_by_ip(self, ip: str) -> Source:
        for source in self.__sources:
            if source._ip == ip:
                return source
        return None

    def process_packet_from_receiver(self, source_ip, data) -> None:
        """Sends data from the main receiver to ffmpeg after verifying it's on our list"""
        source = self.__get_source_by_ip(source_ip)
        if source:
            if self.__check_packet(source, data):
                self.__update_pipe_attributes_open_pipe(source, data[:5])
                source.write(data[5:])  # Write the data to the output fifo
        self.__check_for_inactive_sources()

    def get_status(self) -> None:
        """Returns nothing yet"""
        pass

    def stop(self) -> None:
        """Stops the Sink, closes all handles"""
        self.__running = False

    def run(self) -> None:
        """This thread implements listening to self.fifoin and sending it out to dest_ip"""
        fd = open(self.__fifo_in, "rb")
        while self.__running:
            try:
                header = bytes([0x01, 0x20, 0x02, 0x03, 0x00])  # 48khz, 32-bit, stereo
                data = fd.read(1152)  # Read 1152 bytes from ffmpeg
                if len(data) == 1152:
                    sendbuf = header + data  # Add the header to the data
                    self.__sock.sendto(sendbuf, (self._dest_ip, 4010))  # Send it to the sink
            except Exception as e:
                print(traceback.format_exc())
        print("Stopping " + self.__temp_path)
        self.__close_ffmpeg()
        for fifo_handle in self.__fifo_file_handles:
            try:
                fifo_handle.close()
            except:
                print(traceback.format_exc())


class Receiver(threading.Thread):
    """Handles the main socket that listens for incoming Scream streams and sends them to the appropriate sinks"""
    def __init__(self):
        """Takes no parameters"""
        super().__init__()
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sinks: List[Sink] = []
        self.running: bool = True
        self.start()

    def register_sink(self, sink: Sink) -> None:
        """Add a sink"""
        self.sinks.append(sink)

    def stop(self) -> None:
        """Stops the Receiver and all sinks"""
        self.running = False
        self.sock.close()

    def get_sink_status(self, sink_ip) -> None:
        """For the provided sink, return nothing"""
        for sink in self.sinks:
            if sink._dest_ip == sink_ip:
                return None
        return None

    def run(self) -> None:
        """This thread listens for traffic from all sources and sends it to sinks"""
        self.sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,4096)
        self.sock.bind(("", 16401))

        recvbuf = bytearray(1157)
        while self.running:
            ready = select.select([self.sock], [], [], .2)  # If the socket is dead for more than .2 seconds kill ffmpeg
            if ready[0]:
                try:
                    recvbuf, addr = self.sock.recvfrom(1157)  # 5 bytes header + 1152 bytes pcm
                    for sink in self.sinks:  # Send the data to each recevier, they'll decide if they need to deal with it
                        sink.process_packet_from_receiver(addr[0], recvbuf)
                except Exception as e:
                    print(e)
                    print(traceback.format_exc())
                    break
        print(f"Main thread ending! self.running: {self.running}")
        for sink in self.sinks:
            print("Closing sinks")
            sink.close()
            sink.stop()
            sink.join()