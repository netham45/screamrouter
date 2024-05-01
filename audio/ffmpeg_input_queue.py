"""Handles FFmpeg input queues"""
import collections
import threading
from screamrouter_types import IPAddressType
from logger import get_logger

logger = get_logger(__name__)

class FFMpegInputQueueEntry():
    """A data entry in the ffmpeg input queue, holds the data and the source IP address."""
    tag: str
    """Source IP address for data sent to an input queue"""
    data: bytes
    """Data sent to an input queue"""
    def __init__(self, tag: str, data: bytes):
        self.tag = tag
        self.data = data


class FFMpegInputQueue(threading.Thread):
    """An FFMPEG Input Queue is written to by the receiver, passed to each Sink Controller,
        which passes to ffmpeg. There is one queue per sink controller."""
    def __init__(self, callback, sink_ip: IPAddressType):
        super().__init__(name=f"[Sink:{sink_ip}] ffmpeg Input Queue")
        self._queue: collections.deque = collections.deque()
        """Holds the queue to read/write from"""
        self._running: bool = True
        """Rather the thread is running"""
        self._callback = callback
        """Callback in Sink controller for us to call when there's data available"""
        self.__sink_ip: IPAddressType = sink_ip
        """Holds the sink IP (Only used for log messages)"""
        self._condition: threading.Condition = threading.Condition()
        """Condition"""
        self.start()

    def stop(self) -> None:
        """Stops the queue thread"""
        self._running = False

    def queue(self, entry: FFMpegInputQueueEntry) -> None:
        """Adds an item to the queue"""
        self._queue.append(entry)
        self._condition.acquire()
        self._condition.notify_all()
        self._condition.release()

    def run(self) -> None:
        """Constantly checks the queue
            notifies the Sink Controller callback when there's something in the queue"""
        while self._running:
            while len(self._queue) > 0:
                self._callback(self._queue.popleft())
            self._condition.acquire()
            self._condition.wait(timeout=.2)
            self._condition.release()
        logger.debug("[Sink:%s] Ffmepg Output Queue thread exit", self.__sink_ip)
