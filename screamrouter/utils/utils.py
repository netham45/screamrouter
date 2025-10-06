"""Contains utilities to be called by any class"""

import os

import setproctitle

from screamrouter.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)


def set_process_name(shortname: str = "", fullname: str = "") -> None:
    """Sets the process name so it can be viewed under top.
       Short name is limited to 14 chars."""
    return # Disabled for now
    shortname = f"SR{shortname[:14]}"
    logger.debug("Setting process name for pid %s to: short: %s long %s",
                 os.getpid(), shortname, fullname)
    if len(fullname) > 2:
        setproctitle.setproctitle(f"ScreamRouter ({os.getpid()}): {fullname}")
    if len(shortname) > 0:
        try:
            with open('/proc/self/comm', 'w', encoding="ascii") as f:
                f.write(shortname)
        except FileNotFoundError:
            pass

def close_pipe(fd: int)  -> None:
    """Closes a pipe, ignores oserror"""
    try:
        os.close(fd)
    except OSError:
        pass

def close_all_pipes() -> None:
    """Closes all pipes for the current process"""
    for path in os.listdir("/proc/self/fd"):
        fd: int = int(path)
        if fd > 3:
            close_pipe(fd)
