import queue
import threading


class FFmpegInputQueueEntry():
    """A data entry in the ffmpeg input queue, holds the data and the source IP adderss."""
    source_ip: str
    """Source IP address for data sent to an input queue"""
    data: bytes
    """Data sent to an input queue"""
    def __init__(self, source_ip: str, data: bytes):
        self.source_ip = source_ip
        self.data = data


class FFmpegInputQueue(threading.Thread):
    """An FFMPEG Input Queue is written to by the receiver, verified to belong to the sink by the sink controller, and read from by ffmpeg. There is one queue per sink controller."""
    def __init__(self, callback, sink_ip: str):
        super().__init__(name=f"[Sink {sink_ip}] ffmpeg Input Queue")
        self._queue: queue.Queue = queue.Queue()
        """Holds the queue to read/write from"""
        self._running = True
        """Rather the thread is running"""
        self._callback = callback
        """Callback in Sink controller for us to call when there's data available"""
        self.__sink_ip = sink_ip
        """Holds the sink IP (Only used for log messages)"""
        self.start()

    def stop(self):
        """Stops the queue thread"""
        self._running = False

    def queue(self, entry: FFmpegInputQueueEntry):
        """Adds an item to the queue"""
        self._queue.put_nowait(entry)

    def run(self):
        """Constantly checks the queue, notifies the Sink Controller callback when there's something in the queue"""
        while self._running:
            try:
                entry = self._queue.get(True, .01)  # Blocks until data available
                self._callback(entry)
            except Exception:
                pass
        print(f"[Sink {self.__sink_ip}] Queue thread exit")