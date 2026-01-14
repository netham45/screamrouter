import sys
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

    def acquire(self, timeout=None):
        self.attempts += 1
        return False

    def release(self):
        pass

    def notify(self):
        pass


def noop(*args, **kwargs):
    return None


def test_reload_configuration_times_out_when_condition_unavailable():
    """Reproduces the TimeoutError hit when the reload condition stays locked."""
    manager = ConfigurationManager.__new__(ConfigurationManager)
    manager._safe_async_run = noop  # pylint: disable=protected-access
    manager._broadcast_full_configuration = noop
    manager.reload_condition = LockedCondition()
    manager.reload_config = False

    with pytest.raises(TimeoutError):
        manager._ConfigurationManager__reload_configuration()  # pylint: disable=protected-access
