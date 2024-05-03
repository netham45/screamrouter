"""Holds generic configuration"""
import logging
import os

import constants

def get_logger(name: str) -> logging.Logger:
    """Creates a pre-configured logger"""
    logging.basicConfig()
    if not os.path.exists(constants.LOGS_DIR):
        os.makedirs(constants.LOGS_DIR)
    if constants.CLEAR_LOGS_ON_RESTART:
        for filename in os.listdir(constants.LOGS_DIR):
            os.remove(f"{constants.LOGS_DIR}/{filename}")
    logger = logging.getLogger(name)
    logger.setLevel(constants.CONSOLE_LOG_LEVEL)

    while len(logger.handlers) > 0:
        logger.removeHandler(logger.handlers[0])

    if constants.LOG_TO_FILE:
        file_log_formatter = logging.Formatter(
            '[%(levelname)s:%(asctime)s][%(filename)s:%(lineno)s:%(process)s]%(message)s'
        )

        file_handler = logging.FileHandler(f"{constants.LOGS_DIR}{name}.log", delay=True)
        file_handler.setLevel(logging.DEBUG)
        file_handler.setFormatter(file_log_formatter)

        all_files_file_handler = logging.FileHandler(f"{constants.LOGS_DIR}all.log", delay=True)
        all_files_file_handler.setLevel(logging.INFO)
        all_files_file_handler.setFormatter(file_log_formatter)

        logger.addHandler(all_files_file_handler)
        logger.addHandler(file_handler)
    return logger
