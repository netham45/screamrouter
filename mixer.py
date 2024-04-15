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

class Receiver(threading.Thread):
    def __init__(self, master, destip: str, sourceips: List[str]):
        super().__init__()
        self.temppath = tempfile.gettempdir() + f"/scream-{destip}-"
        self.sourceips = sourceips
        self.fifos = []
        self.destip = destip
        for ip in sourceips:
            self.fifos.append(self.temppath + ip)
        self.fifoin = self.temppath + f"scream-{destip}-in"
        try:
            try:
                os.remove(self.fifoin)
            except:
                pass
            os.mkfifo(self.fifoin)
        except:
            pass
        self.running = True
        self.closed = True
        self.multiplesources = False
        if len(sourceips) > 1:
            self.multiplesources = True
        self.ffmpeg_command=['ffmpeg']
        for ip in self.sourceips:
            self.ffmpeg_command.extend(['-use_wallclock_as_timestamps', 'true', '-f', 's24le', '-ac', '2', '-ar', '48000', '-i', self.temppath + ip])
        if self.multiplesources:
            self.ffmpeg_command.extend(['-filter_complex', '[0]aresample=async=99999[a0],[1]aresample=async=99999[a1],[a0][a1]amix'])
        self.ffmpeg_command.extend(["-y", '-f', 's24le', '-ac', '2', '-ar', '48000', self.fifoin])
        self.ffmpeg = False
        print(self.ffmpeg_command)
        master.addSourcesBySink({self.destip:self.sourceips},self.sourceips,self.fifos,self)
        for fifo in self.fifos:
            try:
                try:
                    os.remove(fifo)
                except:
                    pass
                os.mkfifo(fifo)
            except:
                pass
        self.start()

    def stop(self):
        self.running = False

    def close(self):
        print("Closing " + self.temppath)
        if not self.closed:
            self.closed = True
            if self.ffmpeg:
                self.ffmpeg.kill()
                self.ffmpeg = False

    def open(self):
        print("Opening" + self.temppath)
        if self.closed:
            self.closed = False
            if not self.ffmpeg:
                self.ffmpeg = subprocess.Popen(self.ffmpeg_command, shell=False, stdout=subprocess.PIPE, stdin=subprocess.PIPE)

    def run(self):
        self.fd = open(self.fifoin, "rb")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        while self.running:
            ready = select.select([self.fd], [], [], .2)
            if ready[0]:
                try:
                    header = bytes([0x01, 0x18, 0x02, 0x03, 0x00])  # 48khz, 24-bit, stereo
                    data = self.fd.read(1152)
                    sendbuf = header + data
                    self.sock.sendto(sendbuf, (self.destip, 4010))
                except Exception as e:
                    print("Except")
                    print(e)
            else:
                time.sleep(.2)
        print("Stopping " + self.temppath)


class MasterReceiver(threading.Thread):
    def __init__(self):
        super().__init__()
        self.temppath = tempfile.gettempdir() + f"/scream-"
        self.sourcesbysink = {}
        self.sourceips = []
        self.fifos = []
        self.receivers = []
        self.running = True
        self.sock = []
        self.start()

    def addSourcesBySink(self,sourcesBySink, sourceips, fifos, receiver):
        self.receivers.append(receiver)
        self.sourceips.extend(sourceips)
        self.fifos.extend(fifos)
        self.sourcesbysink.update(sourcesBySink)
        print(self.sourcesbysink)

    def makeFifoName(self, source, sink):
        return self.temppath + f"{sink}-{source}"

    def close(self):
        self.running = False

    def run(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,4096)
        self.sock.bind(("", 16401))

        recvbuf = bytearray(1157)
        framecount = [0] * len(self.sourceips)
        closed = 1
        fifo_files = {}
        while self.running:
            ready = select.select([self.sock], [], [], .2)
            if ready[0]:
                try:
                    recvbuf, addr = self.sock.recvfrom(1157)
                except:
                    break
                if closed == 1:
                    framecount = {}
                    fifo_files = {}
                    for fifo in self.fifos:
                        fd = os.open(fifo,os.O_RDWR)
                        fifo_file = os.fdopen(fd, 'wb', 0)
                        fifo_files[fifo]=fifo_file
                        for receiver in self.receivers:
                            receiver.open()
                        framecount[fifo] = 0
                    closed = 0

                if addr[0] in self.sourceips:
                    for sink, sourceips in self.sourcesbysink.items():
                        key=self.makeFifoName(addr[0],sink)
                        if addr[0] in sourceips:
                            fifo_files[key].write(recvbuf[5:])
                            framecount[key] = framecount[key] + 1

                # Keep buffers roughly in sync while playing
                targetframes=max(framecount.values())
                for key,value in framecount.items():
                    if targetframes - framecount[key] > 11:
                        while (targetframes - framecount[key]) > 0:
                            fifo_files[key].write(bytes([0]*1157))
                            framecount[key] = framecount[key] + 1
            else:
                if closed == 0:
                    for receiver in self.receivers:
                            receiver.close()
                    print("No data, killing ffmpeg")
                    for fifo_file in fifo_files:
                        try:
                            fifo_file.close()
                        except:
                            pass

                    closed = 1
        for receiver in self.receivers:
            print("Closing receivers")
            receiver.close()
            receiver.stop()
        for fifo_file in fifo_files.values():
            fifo_file.close()