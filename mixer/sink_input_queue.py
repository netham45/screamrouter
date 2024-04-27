import queue
import threading


class SinkInputQueueEntry():
    source_ip: str
    data: bytes
    def __init__(self, source_ip: str, data: bytes):
        self.source_ip = source_ip
        self.data = data

class SinkInputQueue(threading.Thread):
    def __init__(self, callback):
        super().__init__(name=f"Sink Input Queue")
        self._queue: queue.Queue = queue.Queue()
        self._running = True
        self._callback = callback
        self.start()

    def stop(self):
        self._running = False

    def queue(self, entry: SinkInputQueueEntry):
        self._queue.put_nowait(entry)

    def run(self):
        while self._running:
            try:
                entry = self._queue.get(True, .1)
                self._callback(entry)
            except:
                pass
        print("Queue thread exit")