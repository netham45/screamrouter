import socket
import threading
import os
import time
import subprocess
import tempfile
import select
from typing import List
from copy import copy
import traceback

class Sink(threading.Thread):
    def __init__(self, master, destip: str, sourceips: List[str]):
        """This gets data from multiple sources from a master, mixes them, and sends them back out to destip"""
        super().__init__()
        self.destip = destip
        self.sourceips = sourceips
        self.temppath = tempfile.gettempdir() + f"/scream-{self.destip}-"

        self.fifos = []  # Holds fifo file names for output to ffmpeg
        self.fifo_files = {}  # Holds fifo file objects
        self.framecount = {}  # Holds number of frames sent per channe;

        for ip in sourceips:  # Build output fifo file name list
            self.fifos.append(self.temppath + ip)

        self.fifoin = self.temppath + "in"  # Input from ffmpeg

        self.running = True  # Running
        self.closed = True  # But not open

        self.lastmessagetime = 0

        self.multiplesources = len(sourceips) > 1  # Are there multiple sources? If not don't add amixer

        self.ffmpeg_command=['ffmpeg', '-hide_banner']  # Build ffmpeg command

        for ip in self.sourceips:  # Add an ffmpeg input for each source #'-use_wallclock_as_timestamps', 'true',
            self.ffmpeg_command.extend(['-use_wallclock_as_timestamps', 'true', '-f', 's24le', '-ac', '2', '-ar', '48000', '-i', f"{self.temppath + ip}"])

        if self.multiplesources:  # If there are multiple sources build a filter_complex string
            filterstring = "" #[0]aresample=async=10000:osr=48000[a0]"
            mixinputs="" #[a0]"

            for i in range(0,len(sourceips)):  # For each source IP add an input to aresample async, and append it to an input variable for amix
                filterstring = filterstring + f"[{i}]aresample=async=10000:osr=48000[a{i}],"  # aresample
                mixinputs = mixinputs + f"[a{i}]"  # amix input
            resultstring = filterstring + mixinputs + "amix"  # Combine the string
            self.ffmpeg_command.extend(['-filter_complex', resultstring])  # Add the filter to ffmpeg command line
        self.ffmpeg_command.extend(['-packetsize', '1152', '-y', '-f', 's24le', '-ac', '2', '-ar', '48000', f"file:{self.fifoin}"])  # ffmpeg output (self.fifoin)
        self.ffmpeg = False  # False if ffmpeg isn't running
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        print(self.ffmpeg_command)
        self.lastmessage = {}
        master.registerSink(self)  # Let the master know to update us
        self.makefifos()  # Make FIFO files
        self.start()  # Start our listener thread

    def makefifos(self,make_fifoin=True):
        """Makes all fifo files for this instance of ffmpeg"""
        try:
            if make_fifoin:
                try:
                    os.remove(self.fifoin)
                except:
                    print(traceback.format_exc())
                os.mkfifo(self.fifoin)
        except:
            print(traceback.format_exc())
        for fifo in self.fifos:
            try:
                try:
                    os.remove(fifo)
                except:
                    print(traceback.format_exc())
                os.mkfifo(fifo)
            except:
                print(traceback.format_exc())

    def stop(self):
        """Stops the thread"""
        self.running = False

    def close(self):
        """Closes the ffmpeg instance"""
        print("Closing " + self.temppath)
        if not self.closed:
            self.closed = True
            try:
                self.ffmpeg.kill()
                self.ffmpeg = False
            except:
                print(traceback.format_exc())
                print(f"Failed to close ffmpeg for {self.temppath}!")

    def open(self):
        """Opens the ffmpeg instance"""
        print("Opening" + self.temppath)
        if self.closed:
            self.makefifos(False)
            self.lastmessagetime = time.time()*1000.0
            self.framecount = {}
            self.fifo_files = {}
            for fifo in self.fifos:
                fd = os.open(fifo,os.O_RDWR | os.O_NONBLOCK)
                fifo_file = os.fdopen(fd, 'wb', 0)
                self.fifo_files[fifo]=fifo_file
                self.fifo_files[fifo].write(bytes([0]*1152*4))
                self.framecount[fifo] = 0
                self.lastmessage[fifo] = bytes([0] * 1152)
            self.closed = False
            if not self.ffmpeg:
                self.ffmpeg = subprocess.Popen(self.ffmpeg_command, shell=False, stdout=subprocess.PIPE, stdin=subprocess.PIPE)

    def send(self, sourceip, data):
        """Sends data from the master to ffmpeg after verifying it's on our list"""
        now = time.time()*1000.0
        if (now - self.lastmessagetime) > 150 and not self.closed:
            self.close()
        self.lastmessagetime = now
        if sourceip in self.sourceips:
            key = self.temppath + sourceip
            if self.closed:
                self.open()
            if len(data) != 1157:
                return
            self.fifo_files[key].write(data[5:])  # Write the data to the output fifo
            self.lastmessage[key] = copy(data[5:])
            self.framecount[key] = self.framecount[key] + 1

            targetframes=max(self.framecount.values())   # If any source gets more than N packets out of sync catch it up
            for key,value in self.framecount.items():
                if targetframes - self.framecount[key] > 11:
                    self.fifo_files[key].write(self.lastmessage[key]*12)
                    self.framecount[key] = self.framecount[key] + 12

    def run(self):
        """This thread implements listening to self.fifoin and sending it out to destip when it's written to"""
        self.fd = open(self.fifoin, "rb")
        while self.running:
            ready = select.select([self.fd], [], [], .2)  # If the fifo is unavailable for .2 seconds then assume something is wrong
            if ready[0]:
                try:
                    header = bytes([0x01, 0x18, 0x02, 0x03, 0x00])  # 48khz, 24-bit, stereo
                    data = self.fd.read(1152)  # Read 1152 bytes from ffmpeg
                    sendbuf = header + data  # Add the header to the data
                    self.sock.sendto(sendbuf, (self.destip, 4010))  # Send it to the sink
                except Exception as e:
                    print(traceback.format_exc())
        print("Stopping " + self.temppath)
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

    def registerSink(self, sink: Sink):
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
                        sink.send(addr[0],recvbuf)
                except Exception as e:
                    print(e)
                    print(traceback.format_exc())
                    break
        print(f"Main thread ending! self.running: {self.running}")
        for sink in self.sinks:
            print("Closing sinks")
            sink.close()
            sink.stop()