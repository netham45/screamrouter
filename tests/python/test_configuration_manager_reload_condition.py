import concurrent.futures
import sys
import threading
from pathlib import Path

import pytest

ROOT_DIR = Path(__file__).resolve().parents[2]
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from screamrouter.configuration.configuration_manager import ConfigurationManager


class LockedCondition:
    """Condition stub that never grants the lock."""

    def __init__(self):
        self.attempts = 0
        self._lock = threading.Lock()

    def acquire(self, timeout=None):
        with self._lock:
            self.attempts += 1
        return False

    def release(self):
        pass

    def notify(self):
        pass


def noop(*args, **kwargs):
    return None


def test_reload_configuration_times_out_when_condition_unavailable():
    """Ensure many simultaneous reload requests all time out when the condition is locked."""
    manager = ConfigurationManager.__new__(ConfigurationManager)
    manager._safe_async_run = noop  # pylint: disable=protected-access
    manager._broadcast_full_configuration = noop
    manager.reload_condition = LockedCondition()
    manager.reload_config = False
    manager._lock_holder_thread_id = None
    manager._lock_holder_stack = None

    def attempt_reload_timeout():
        try:
            manager._ConfigurationManager__reload_configuration()  # pylint: disable=protected-access
        except TimeoutError:
            return True
        return False

    parallel_changes = 64
    with concurrent.futures.ThreadPoolExecutor(max_workers=parallel_changes) as executor:
        futures = [executor.submit(attempt_reload_timeout) for _ in range(parallel_changes)]

    results = [future.result(timeout=1) for future in futures]

    assert all(results), "Every reload attempt should time out when the condition never unlocks"
    assert manager.reload_condition.attempts >= parallel_changes
