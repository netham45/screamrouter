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
        sample_rate_bits = numpy.unpackbits(numpy.array([scream_header[0]], dtype=numpy.uint8), bitorder='little')
        sample_rate_base = 44100 if sample_rate_bits[7] == 1 else 48000
        sample_rate_bits = numpy.delete(sample_rate_bits,7)
        sample_rate_multiplier = numpy.packbits(sample_rate_bits,bitorder='little')[0]
        if sample_rate_multiplier < 1:
            sample_rate_multiplier = 1
        self.sample_rate = sample_rate_base * sample_rate_multiplier
        self.bit_depth = scream_header[1]
        self.channels = scream_header[2]
        self.map = scream_header[3:]

    def __eq__(self, other):
        return (self.sample_rate == other.sample_rate) and (self.bit_depth == other.bit_depth) and (self.channels == other.channels)

class Sink(threading.Thread):
    def __init__(self, master, dest_ip: str, source_ips: List[str]):
        """This gets data from multiple sources from a master, mixes them, and sends them back out to destip"""
        super().__init__()
        self.dest_ip = dest_ip
        self.source_ips = source_ips
        self.temp_path = tempfile.gettempdir() + f"/scream-{self.dest_ip}-"  # Per-sink temp path

        self.fifo_names = []  # Holds fifo file names for output to ffmpeg, these are used as keys for all other dicts
        self.fifo_files = {}  # Holds fifo file objects
        self.active_ips = []  # Holds active source IPs
        self.fifo_open = {}  # Holds rather a fifo is open or not
        self.last_message_time = {}  # Holds the last message time for a fifo

        self.scream_info = {}  # Holds scream stream status, stream is restarted when this changesa

        self.fifoin = self.temp_path + "in"  # Input file from ffmpeg
        for ip in self.source_ips:  # Build output fifo file name list
            key = self.temp_path + ip
            self.fifo_names.append(key)
            self.scream_info[key] = ScreamStreamInfo([129, 0x10, 0x02, 0x03, 0x00])
            self.last_message_time[key] = 0
            self.fifo_open[key] = False

        self.running = True  # Running
        self.closed = True  # But not open

        self.ffmpeg = False  # False if ffmpeg isn't running

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # Socket to send to sink

        self.make_in_fifo()  # Make ffmpeg output fifo
        self.make_out_fifos()  # Make ffmpeg input fifo
        master.register_sink(self)  # Let the master know to update us
        self.start()  # Start our listener thread

    def build_active_ips(self):
        """Build a list of active IPs, exclude ones that aren't open"""
        self.active_ips = []
        for ip in self.source_ips:
            if self.fifo_open[self.temp_path + ip]:
                self.active_ips.append(ip)

    def build_ffmpeg_inputs(self):
        """Add an input for each source"""
        ffmpeg_command = []
        for ip in self.active_ips:
            key = self.temp_path + ip
            bit_depth = self.scream_info[key].bit_depth
            sample_rate = self.scream_info[key].sample_rate
            channels = self.scream_info[key].channels
            ffmpeg_command.extend(['-thread_queue_size', '64', '-f', f's{bit_depth}le', '-ac', f'{channels}', '-ar', f'{sample_rate}', '-i', f"{self.temp_path + ip}"])
        return ffmpeg_command

    def build_ffmpeg_filters(self):
        """Build complex filter"""
        ffmpeg_command = []
        multiple_sources = len(self.active_ips) > 1

        filter_string = ""
        amix_inputs = ""

        for i in range(0,len(self.active_ips)):  # For each source IP add an input to aresample async, and append it to an input variable for amix
            filter_string = filter_string + f"[{i}]asetpts='(RTCTIME - RTCSTART) / (TB * 1000000)',aresample=async=5000:flags=+res:resampler=soxr[a{i}],"
            amix_inputs = amix_inputs + f"[a{i}]"  # amix input

        result = (filter_string + amix_inputs + "aresample[0]") if not multiple_sources else (filter_string + amix_inputs + "amix=normalize=0")  # Combine the string
        ffmpeg_command.extend(['-filter_complex', result])  # Add the filter to ffmpeg command line
        return ffmpeg_command

    def build_ffmpeg_command(self):
        """Builds the ffmpeg command"""
        ffmpeg_command=['ffmpeg', '-hide_banner']  # Build ffmpeg command
        self.build_active_ips()
        ffmpeg_command.extend(self.build_ffmpeg_inputs())
        ffmpeg_command.extend(self.build_ffmpeg_filters())
        ffmpeg_command.extend(['-y', '-f', 's32le', '-ac', '2', '-ar', '48000', f"file:{self.fifoin}"])  # ffmpeg output

        print(ffmpeg_command)
        return ffmpeg_command

    def make_in_fifo(self):
        """Makes fifoin for ffmpeg to send back to python"""
        try:
            try:
                os.remove(self.fifoin)
            except:
                print(traceback.format_exc())
            os.mkfifo(self.fifoin)
        except:
            print(traceback.format_exc())

    def make_out_fifos(self):
        """Makes all fifo files for this instance of ffmpeg"""
        for key in self.fifo_names:
            try:
                try:
                    os.remove(key)
                except:
                    print(traceback.format_exc())
                os.mkfifo(key)
                fd = os.open(key,os.O_RDWR | os.O_NONBLOCK)
                self.fifo_files[key] = os.fdopen(fd, 'wb', 0)
            except:
                print(traceback.format_exc())

    def stop(self):
        """Stops the thread"""
        self.running = False

    def close(self):
        """Closes the ffmpeg instance"""
        if not self.closed:
            self.closed = True
            try:
                self.ffmpeg.kill()
                self.ffmpeg.wait()
                self.ffmpeg = False
            except:
                print(traceback.format_exc())
                print(f"Failed to close ffmpeg for {self.temp_path}!")

    def open(self):
        """Opens the ffmpeg instance"""
        print("Opening" + self.temp_path)
        if self.closed:
            if not self.ffmpeg:
                self.ffmpeg = subprocess.Popen(self.build_ffmpeg_command(), shell=False, stdout=subprocess.PIPE, stdin=subprocess.PIPE)
            self.closed = False

    def check_for_old_pipes(self):
        """Looks for old pipes that are open and closes them"""
        now = time.time() * 1000.0
        for key, value in self.last_message_time.items():
                if not self.fifo_open[key]:
                    continue
                if now - value > 200:
                    self.fifo_open[key] = False
                    print(f"No frames from {key} in 200ms, restarting ffmpeg")
                    self.close()

    def send(self, sourceip, data):
        """Sends data from the master to ffmpeg after verifying it's on our list"""
        if len(data) != 1157:
                print(f"Got bad packet length {len(data)}")
                return

        if sourceip in self.source_ips:  # Check if the source is one we care about
            now = time.time() * 1000.0
            key = self.temp_path + sourceip
            scream_header = ScreamStreamInfo(data[:5])

            if scream_header != self.scream_info[key]:  # Have input stream properties changed?
                print(f"{key} had a stream property change. Was: {self.scream_info[key].bit_depth}-bit at {self.scream_info[key].sample_rate}kHz, is now {scream_header.bit_depth}-bit at {scream_header.sample_rate}kHz.")
                self.scream_info[key] = scream_header
                self.fifo_open[key] = True
                self.close()  # Close to reopen with the new input format properties

            if (now - self.last_message_time[key] > 100) or not self.fifo_open[key]:  # Is it writing to a closed fifo?
                self.fifo_open[key] = True  # Mark the fifo as open and close to reopen with the new fifo
                print(f"Trying to write to a closed pipe {key}, restarting ffmpeg")
                self.close()

            if self.closed:  # Is the sink closed?
                self.open()

            self.fifo_files[key].write(data[5:])  # Write the data to the output fifo
            self.last_message_time[key] = now

        self.check_for_old_pipes()

    def run(self):
        """This thread implements listening to self.fifoin and sending it out to destip when it's written to"""
        self.fd = open(self.fifoin, "rb")
        while self.running:
            try:
                header = bytes([0x01, 0x20, 0x02, 0x03, 0x00])  # 48khz, 32-bit, stereo
                data = self.fd.read(1152)  # Read 1152 bytes from ffmpeg
                if len(data) == 1152:
                    sendbuf = header + data  # Add the header to the data
                    self.sock.sendto(sendbuf, (self.dest_ip, 4010))  # Send it to the sink
            except Exception as e:
                print(traceback.format_exc())
        print("Stopping " + self.temp_path)
        self.close()
        for fifo_file in self.fifo_files:
            try:
                fifo_file.close()
            except:
                print(traceback.format_exc())


class Receiver(threading.Thread):
    def __init__(self):
        """Takes no parameters"""
        super().__init__()
        self.running = True
        self.sock = []  # Holds the local UDP socket that listens for data
        self.sinks = []  # Holds the sinksto send data to
        self.start()

    def register_sink(self, sink: Sink):
        """Add a sink"""
        self.sinks.append(sink)

    def close(self):
        """Close the socket, stop the thread"""
        self.sock.close()
        self.running = False

    def run(self):
        """This thread listens for traffic from all sources and sends it to sinks"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
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