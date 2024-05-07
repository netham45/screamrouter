"""Contains utilities to be called by any class"""

import os

import setproctitle

from src.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)


def set_process_name(shortname: str = "", fullname: str = ""):
    """Sets the process name so it can be viewed under top.
       Short name is limited to 14 chars."""
    shortname = shortname[:16]
    logger.debug("Setting process name for pid %s to: short: %s long %s",
                 os.getpid(), shortname, fullname)
    if len(fullname) > 2:
        setproctitle.setproctitle(f"ScreamRouter ({os.getpid()}): {fullname}")
    if len(shortname) > 0:
        with open('/proc/self/comm', 'w', encoding="ascii") as f:
            f.write(shortname)
