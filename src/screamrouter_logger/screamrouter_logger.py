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

if constants.LOG_TO_FILE:
    MAIN_LOGGER = logging.getLogger()

    all_rotating_handler = logging.handlers.RotatingFileHandler(f"{constants.LOGS_DIR}all.log",
                                    maxBytes=10000000, backupCount=constants.LOG_ENTRIES_TO_RETAIN)
    all_rotating_handler.setLevel(logging.INFO)
    all_rotating_handler.setFormatter(FORMATTER)
    MAIN_LOGGER.addHandler(all_rotating_handler)
    all_rotating_handler.doRollover()

def get_logger(name: str) -> logging.Logger:
    """Creates a pre-configured logger"""
    logger = logging.getLogger(name)
    logger.propagate = False

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
        rotating_handler.doRollover()
    return logger
