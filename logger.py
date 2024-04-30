"""Holds generic configuration"""
import logging
import os

import yaml

LOGS_DIR = "logs/"
CONSOLE_LOG_LEVEL = "INFO"

try:
    with open("config.yaml", "r", encoding="UTF-8") as f:
        config = yaml.safe_load(f)
        try:
            LOGS_DIR = config['server']['log_path']
        except KeyError:
            pass
        except IndexError:
            pass
        try:
            CONSOLE_LOG_LEVEL = config['server']['console_log_level'].upper()
        except KeyError:
            pass
        except IndexError:
            pass
except FileNotFoundError:
    pass


def get_logger(name: str) -> logging.Logger:
    """Creates a pre-configured logger"""
    if not os.path.exists(LOGS_DIR):
        os.makedirs(LOGS_DIR)
    logger = logging.getLogger(name)
    logger.setLevel(CONSOLE_LOG_LEVEL)
    file_log_formatter = logging.Formatter(
        '[%(levelname)s:%(asctime)s][%(filename)s:%(lineno)s:%(process)s]%(message)s'
    )

    
    file_handler = logging.FileHandler(f"{LOGS_DIR}{name}.log", delay=True)
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(file_log_formatter)

    all_files_file_handler = logging.FileHandler(f"{LOGS_DIR}all.log", delay=True)
    all_files_file_handler.setLevel(logging.INFO)
    all_files_file_handler.setFormatter(file_log_formatter)

    while len(logger.handlers) > 0:
        logger.removeHandler(logger.handlers[0])
    logger.addHandler(all_files_file_handler)
    logger.addHandler(file_handler)
    return logger
