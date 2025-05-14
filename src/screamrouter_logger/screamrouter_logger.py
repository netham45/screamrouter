"""ScreamRouter Logger"""
import logging
import logging.handlers
import os
import sys

import src.constants.constants as constants

if not os.path.exists(constants.LOGS_DIR):
    os.makedirs(constants.LOGS_DIR)

FORMATTER = logging.Formatter("".join(['[%(levelname)s:%(asctime)s]',
                                       '[%(filename)s:%(lineno)s:%(process)s]%(message)s']))

# Configure root logger
MAIN_LOGGER = logging.getLogger()
MAIN_LOGGER.setLevel(logging.DEBUG)

# Console handler for root logger
root_console = logging.StreamHandler(sys.stdout)
root_console.setLevel(logging.INFO)
root_console.setFormatter(FORMATTER)
MAIN_LOGGER.addHandler(root_console)

if constants.LOG_TO_FILE:
    all_rotating_handler = logging.handlers.RotatingFileHandler(f"{constants.LOGS_DIR}all.log",
                                    maxBytes=10000000, backupCount=constants.LOG_ENTRIES_TO_RETAIN)
    all_rotating_handler.setLevel(logging.INFO)
    all_rotating_handler.setFormatter(FORMATTER)
    MAIN_LOGGER.addHandler(all_rotating_handler)
    try:
        all_rotating_handler.doRollover()
    except:
        pass

def get_logger(name: str) -> logging.Logger:
    """Creates a pre-configured logger"""
    logger = logging.getLogger(name)
    logger.propagate = False
    logger.setLevel(logging.DEBUG)  # Set base logger level to allow all messages through

    console = logging.StreamHandler(sys.stderr)
    console.setLevel(constants.CONSOLE_LOG_LEVEL)
    console.setFormatter(FORMATTER)
    logger.addHandler(console)

    if constants.LOG_TO_FILE:
        rotating_handler = logging.handlers.RotatingFileHandler(f"{constants.LOGS_DIR}{name}.log",
                                    maxBytes=100000, backupCount=constants.LOG_ENTRIES_TO_RETAIN)
        rotating_handler.setLevel(logging.DEBUG)
        rotating_handler.setFormatter(FORMATTER)


        logger.addHandler(rotating_handler)
        try:
            rotating_handler.doRollover()
        except:
            pass
    return logger
