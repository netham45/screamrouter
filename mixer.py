import socket
import struct
import threading
import wave
import os
import sys
import time
import subprocess
import tempfile
import select
from typing import List, Type
from copy import copy
import traceback
import numpy

class ScreamStreamInfo():
    def __init__(self, scream_header):
        """Parses the first five bytes of a Scream header to get the stream attributes"""
        sample_rate_bits = numpy.unpackbits(numpy.array([scream_header[0]], dtype=numpy.uint8), bitorder='little')  # Unpack the first byte into 8 bits
        sample_rate_base = 44100 if sample_rate_bits[7] == 1 else 48000  # If the uppermost bit is set then the base is 44100, if it's not set the base is 48000
        sample_rate_bits = numpy.delete(sample_rate_bits, 7)  # Remove the uppermost bit
        sample_rate_multiplier = numpy.packbits(sample_rate_bits,bitorder='little')[0]  # Convert it back into a number without the top bit, this is the multiplier to multiply the base by
        if sample_rate_multiplier < 1:
            sample_rate_multiplier = 1
        self.sample_rate = sample_rate_base * sample_rate_multiplier
        self.bit_depth = scream_header[1]  # One byte for bit depth
        self.channels = scream_header[2]  # One byte for channel count
        self.map = scream_header[3:]  # Two bytes for WAVEFORMATEXTENSIBLE

    def __eq__(self, other):
        return (self.sample_rate == other.sample_rate) and (self.bit_depth == other.bit_depth) and (self.channels == other.channels)

class Sink(threading.Thread):
    def __init__(self, receiver, dest_ip: str, source_ips: List[str]):
        """This gets data from multiple sources from a master, mixes them, and sends them back out to destip"""
        super().__init__()
        self._dest_ip = dest_ip
        self._source_ips = source_ips
        self.__temp_path = tempfile.gettempdir() + f"/scream-{self._dest_ip}-"  # Per-sink temp path

        self._keys = []  # Holds keys the rest of the dicts use

        self.__fifoin = self.__temp_path + "in"  # Input file from ffmpeg
        self._fifo_file_names = {}  # Holds fifo file names for output to ffmpeg
        self.__fifo_file_handles = {}  # Holds fifo file objects

        self._source_open = {}  # Holds rather a source is open or not
        self._last_data_time = {}  # Holds the last data received time for a source
        self._stream_attributes = {}  # Holds scream stream attributes, stream is restarted when this changes

        self.__running = True  # Running

        self.__ffmpeg = False  # Holds popen for ffmpeg once it's started
        self.__sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # Socket to send to sink

        for ip in self._source_ips:  # Initialize dicts based off of source ips
            key = ip
            self._keys.append(key)
            self._fifo_file_names[key] =  self.__temp_path + ip
            self._stream_attributes[key] = ScreamStreamInfo([0] * 5)
            self._last_data_time[key] = 0
            self._source_open[key] = False

        self.__make_in_fifo()  # Make python -> ffmpeg fifo fifo
        self.__make_out_fifos()  # Make ffmpeg fifo->python->sink fifo
        self.start()  # Start our listener thread

    def __get_active_sources(self):
        """Build a list of active IPs, exclude ones that aren't open"""
        active_sources = []
        for ip in self._source_ips:
            key = ip
            if self._source_open[key]:
                active_sources.append(ip)
        return active_sources

    def __get_ffmpeg_inputs(self, active_sources):
        """Add an input for each source"""
        ffmpeg_command = []
        for ip in active_sources:
            key = ip
            bit_depth = self._stream_attributes[key].bit_depth
            sample_rate = self._stream_attributes[key].sample_rate
            channels = self._stream_attributes[key].channels
            ffmpeg_command.extend(['-thread_queue_size', '64', 
                                   '-f', f's{bit_depth}le', 
                                   '-ac', f'{channels}', 
                                   '-ar', f'{sample_rate}', 
                                   '-i', f'{self.__temp_path + ip}'])
        return ffmpeg_command

    def __get_ffmpeg_filters(self, active_sources):
        """Build complex filter"""
        ffmpeg_command = []
        full_filter_string = ""
        amix_inputs = ""
        per_input_filter = "asetpts='(RTCTIME - RTCSTART) / (TB * 1000000)',aresample=async=5000:flags=+res:resampler=soxr"

        for i in range(0,len(active_sources)):  # For each source IP add an input to aresample async, and append it to an input variable for amix
            full_filter_string = full_filter_string + f"[{i}]{per_input_filter}[a{i}],"
            amix_inputs = amix_inputs + f"[a{i}]"  # amix input
        if len(active_sources) > 1:
            ffmpeg_command.extend(['-filter_complex', full_filter_string + amix_inputs + "amix=normalize=0"])
        else:
            ffmpeg_command.extend(['-filter_complex', full_filter_string + amix_inputs + "aresample[0]"])
        return ffmpeg_command

    def __get_ffmpeg_command(self):
        """Builds the ffmpeg command"""
        active_sources = self.__get_active_sources()

        ffmpeg_command=['ffmpeg', '-hide_banner']  # Build ffmpeg command
        ffmpeg_command.extend(self.__get_ffmpeg_inputs(active_sources))
        ffmpeg_command.extend(self.__get_ffmpeg_filters(active_sources))
        ffmpeg_command.extend(['-y', '-f', 's32le', '-ac', '2', '-ar', '48000', f"file:{self.__fifoin}"])  # ffmpeg output
        return ffmpeg_command

    def __make_in_fifo(self):
        """Makes fifo in for ffmpeg to send back to python"""
        try:
            try:
                os.remove(self.__fifoin)
            except:
                pass
            os.mkfifo(self.__fifoin)
        except:
            print(traceback.format_exc())

    def __make_out_fifos(self):
        """Makes all fifo out files for this instance of ffmpeg"""
        for key in self._keys:
            try:
                try:
                    os.remove(self._fifo_file_names[key])
                except:
                    pass
                os.mkfifo(self._fifo_file_names[key])
                fd = os.open(self._fifo_file_names[key],os.O_RDWR | os.O_NONBLOCK)
                self.__fifo_file_handles[key] = os.fdopen(fd, 'wb', 0)
            except:
                print(traceback.format_exc())

    def __reset_ffmpeg(self):
        """Opens the ffmpeg instance"""
        print("Opening" + self.__temp_path)
        if self.__ffmpeg:
            try:
                self.__ffmpeg.kill()
                self.__ffmpeg.wait()
            except:
                print(traceback.format_exc())
                print(f"Failed to close ffmpeg for {self.__temp_path}!")
        self.__ffmpeg = subprocess.Popen(self.__get_ffmpeg_command(), shell=False, stdout=subprocess.PIPE, stdin=subprocess.PIPE)

    def __check_for_inactive_pipes(self):
        """Looks for old pipes that are open and closes them"""
        now = time.time() * 1000.0
        for key, value in self._last_data_time.items():
            # If the source is open and there's at least one other source open and it's been inactive for 200ms then close it
            if self._source_open[key] and len(self.__get_active_sources()) > 1 and now - value > 200:
                self._source_open[key] = False
                print(f"No frames from {key} in 200ms, restarting ffmpeg")
                self.__reset_ffmpeg()

    def __verify_pipe_open_and_attributes(self, key, header):
        """Verifies the target pipe is open and has the right header. True = Everything valid, False = ffmpeg restarted"""
        if  self.__verify_pipe_attributes(key, header):
            return self.__verify_pipe_is_open(key)
        return False

    def __verify_pipe_attributes(self, key, header):
        """Verifies the target pipe header matches what we have open, updates it if not. False = closed and reopened, True = open already"""
        parsed_scream_header = ScreamStreamInfo(header)
        if parsed_scream_header != self._stream_attributes[key]:  # Have input stream properties changed?
            print(f"{key} had a stream property change. Was: {self._stream_attributes[key].bit_depth}-bit at {self._stream_attributes[key].sample_rate}kHz, is now {parsed_scream_header.bit_depth}-bit at {parsed_scream_header.sample_rate}kHz.")
            self._stream_attributes[key] = parsed_scream_header
            self._source_open[key] = True
            self.__reset_ffmpeg()
            return False
        return True

    def __verify_pipe_is_open(self, key):
        """Verifies the target pipe is open, opens it if not. False = closed and reopened, True = open already"""
        if not self._source_open[key]:  # Is it writing to a closed fifo?
            print(f"Trying to write to a closed pipe {key}, restarting ffmpeg to reopen")
            self._source_open[key] = True  # Mark the fifo as open and close to reopen with the new fifo
            self.__reset_ffmpeg()
            return False
        return True

    def __verify_packet(self, data):
        """Verifies a packet is the right length"""
        if len(data) != 1157:
            print(f"Got bad packet length {len(data)}")
            return False
        return True

    def send(self, source_ip, data):
        """Sends data from the master to ffmpeg after verifying it's on our list"""
        if not self.__verify_packet(data):
            return
   
        if source_ip in self._source_ips:  # Check if the source is one we care about
            key = source_ip
            self.__verify_pipe_open_and_attributes(key, data[:5])
            self.__check_for_inactive_pipes()
            now = time.time() * 1000.0
            self.__fifo_file_handles[key].write(data[5:])  # Write the data to the output fifo
            self._last_data_time[key] = now

    def get_status(self):
        """Returns a dict holding source ip -> source open, dict holding source ip -> stream attributes, list of active source ips"""
        return copy(self._source_open), copy(self._stream_attributes), copy(self._active_sources)

    def stop(self):
        """Stops the thread"""
        self.__running = False

    def run(self):
        """This thread implements listening to self.fifoin and sending it out to dest_ip"""
        self.fd = open(self.__fifoin, "rb")
        while self.__running:
            try:
                header = bytes([0x01, 0x20, 0x02, 0x03, 0x00])  # 48khz, 32-bit, stereo
                data = self.fd.read(1152)  # Read 1152 bytes from ffmpeg
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
    def __init__(self):
        """Takes no parameters"""
        super().__init__()
        self.running = True
        self.sinks = []  # Holds the sinks to send data to
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.start()

    def register_sink(self, sink: Sink):
        """Add a sink"""
        self.sinks.append(sink)

    def stop(self):
        """Close the socket, stop the thread"""
        self.sock.close()
        self.running = False

    def get_sink_status(self, sink_ip):
        """For the provided sink, returns a dict holding source ip -> source open, dict holding source ip -> stream attributes, list of active source ips"""
        for sink in self.sinks:
            if sink._dest_ip == sink_ip:
                return sink.get_status()

    def run(self):
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
                        sink.send(addr[0], recvbuf)
                except Exception as e:
                    print(e)
                    print(traceback.format_exc())
                    break
        print(f"Main thread ending! self.running: {self.running}")
        for sink in self.sinks:
            print("Closing sinks")
            sink.close()
            sink.stop()