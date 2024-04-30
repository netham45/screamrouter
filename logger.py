"""Holds generic configuration"""
import logging
import os
import sys

def get_logger(name: str, log_level: int = logging.INFO) -> logging.Logger:
    """Creates a pre-configured logger"""
    logger = logging.getLogger(name)
    logger.setLevel(log_level)
    stdout_log_formatter = logging.Formatter(
       '[%(levelname)s:%(asctime)s][%(filename)s:%(lineno)s]%(message)s'
    )
    file_log_formatter = logging.Formatter(
        '[%(levelname)s:%(asctime)s][%(filename)s:%(lineno)s:%(process)s]%(message)s'
    )

    stdout_log_handler = logging.StreamHandler(stream=sys.stdout)
    stdout_log_handler.setLevel(logging.INFO)
    stdout_log_handler.setFormatter(stdout_log_formatter)
    if not os.path.exists("logs/"):
        os.mkdir("logs")
    file_handler = logging.FileHandler(f"logs/{name}.log", delay=True)
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(file_log_formatter)

    all_files_file_handler = logging.FileHandler("logs/all.log", delay=True)
    all_files_file_handler.setLevel(logging.DEBUG)
    all_files_file_handler.setFormatter(file_log_formatter)

    while len(logger.handlers) > 0:
        logger.removeHandler(logger.handlers[0])
    logger.addHandler(all_files_file_handler)
    logger.addHandler(file_handler)
    logger.addHandler(stdout_log_handler)
    logger.setLevel(logging.INFO)
    logger.setLevel(log_level)
    return logger

global_logger = get_logger("ScreamRouter")
"""Holds the global logger"""
