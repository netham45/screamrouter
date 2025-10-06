"""ScreamRouter Logger"""
import atexit
import logging
import logging.handlers
import os
import sys
import threading
import time  # Already imported, but good to ensure

# Attempt to import the C++ audio engine bindings
try:
    import screamrouter_audio_engine
except ImportError:
    screamrouter_audio_engine = None
    print("WARNING: C++ audio engine bindings (screamrouter_audio_engine) not found. C++ logs will not be processed.", file=sys.stderr)

import screamrouter.constants.constants as constants

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
    # Convert string log level from constants to integer log level
    try:
        level_int = logging.getLevelName(constants.CONSOLE_LOG_LEVEL.upper())
        if not isinstance(level_int, int): # getLevelName returns int for valid names, string for invalid
            MAIN_LOGGER.warning(f"Invalid CONSOLE_LOG_LEVEL string: '{constants.CONSOLE_LOG_LEVEL}'. Defaulting console for '{name}' to INFO.")
            level_int = logging.INFO
    except Exception as e:
        MAIN_LOGGER.error(f"Error processing CONSOLE_LOG_LEVEL '{constants.CONSOLE_LOG_LEVEL}': {e}. Defaulting console for '{name}' to INFO.", exc_info=True)
        level_int = logging.INFO
        
    console.setLevel(level_int)
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

# --- C++ Log Handling ---

CPP_LOGGER_NAME = "screamrouter.cpp_audio_engine"
# CPP_LOG_QUEUE and related threading primitives for Python-side queue are removed.

# Globals for the new C++ log retrieval mechanism
_cpp_log_retrieval_thread = None
_stop_cpp_log_retrieval_thread = threading.Event()
# Poll interval is effectively managed by the timeout in get_cpp_log_messages

def _cpp_log_retrieval_worker():
    """Worker thread that retrieves and processes C++ log messages."""
    logger = logging.getLogger(CPP_LOGGER_NAME) # Get the pre-configured logger

    MAIN_LOGGER.info("C++ Log Retrieval Worker started.")
    while not _stop_cpp_log_retrieval_thread.is_set():
        try:
            if not screamrouter_audio_engine: # Check if module is available
                MAIN_LOGGER.warning("C++ audio engine not available in log retrieval worker. Stopping.")
                break

            # This call will block on the C++ side until messages are available or timeout
            log_entries_tuples = screamrouter_audio_engine.get_cpp_log_messages(timeout_ms=1000) # e.g., 1 second timeout

            if _stop_cpp_log_retrieval_thread.is_set() and not log_entries_tuples:
                # If stop is set and we got an empty batch (likely due to timeout or C++ shutdown), exit loop
                break

            for entry_tuple in log_entries_tuples:
                if len(entry_tuple) == 4:
                    cpp_level_from_c, message, cpp_filename, cpp_lineno = entry_tuple
                    
                    python_level_int = _map_cpp_level_to_python(cpp_level_from_c)

                    record = logging.LogRecord(
                        name=CPP_LOGGER_NAME,
                        level=python_level_int,
                        pathname=cpp_filename,
                        lineno=cpp_lineno,
                        msg=message,
                        args=(),
                        exc_info=None,
                        func="N/A_CPP_POLL"
                    )
                    logger.handle(record)
                else:
                    MAIN_LOGGER.error(f"Received malformed log entry tuple from C++: {entry_tuple}")

        except AttributeError as ae:
             # screamrouter_audio_engine might become None during shutdown
            if screamrouter_audio_engine is None or "screamrouter_audio_engine" in str(ae):
                MAIN_LOGGER.info("screamrouter_audio_engine became None in log retrieval worker, likely during shutdown. Stopping worker.")
                break
            else:
                MAIN_LOGGER.error(f"AttributeError in C++ log retrieval worker: {ae}", exc_info=True)
                _stop_cpp_log_retrieval_thread.wait(1.0) # Avoid busy loop on unexpected errors
        except RuntimeError as re: # Catches pybind11 errors like "std::runtime_error: Internal log queue is being shut down"
            if "shut down" in str(re).lower():
                 MAIN_LOGGER.info(f"C++ log retrieval worker: C++ logger is shutting down. Stopping. ({re})")
                 break
            else:
                MAIN_LOGGER.error(f"RuntimeError in C++ log retrieval worker: {re}", exc_info=True)
                _stop_cpp_log_retrieval_thread.wait(1.0) # Avoid busy loop
        except Exception as e:
            MAIN_LOGGER.error(f"Unexpected error in C++ log retrieval worker: {e}", exc_info=True)
            # In case of other errors, wait a bit before retrying to avoid tight loop if error is persistent
            _stop_cpp_log_retrieval_thread.wait(1.0)
    MAIN_LOGGER.info("C++ Log Retrieval Worker finished.")


def _map_cpp_level_to_python(cpp_level):
    """Maps C++ LogLevel enum (passed as int or pybind11 enum) to Python logging level."""
    if screamrouter_audio_engine: # Check if module was imported
        if cpp_level == screamrouter_audio_engine.LogLevel_CPP.DEBUG:
            return logging.DEBUG
        if cpp_level == screamrouter_audio_engine.LogLevel_CPP.INFO:
            return logging.INFO
        if cpp_level == screamrouter_audio_engine.LogLevel_CPP.WARNING:
            return logging.WARNING
        if cpp_level == screamrouter_audio_engine.LogLevel_CPP.ERROR:
            return logging.ERROR
    return logging.INFO # Default if mapping fails or module not found

# cpp_log_handler is no longer used as C++ does not call a Python callback directly.
# It's replaced by the _cpp_log_retrieval_worker polling mechanism.


def initialize_cpp_log_forwarding():
    """Initializes the C++ log forwarding mechanism by starting the retrieval thread."""
    global _cpp_log_retrieval_thread
    if screamrouter_audio_engine:
        if _cpp_log_retrieval_thread is None or not _cpp_log_retrieval_thread.is_alive():
            try:
                # Ensure the target Python logger for C++ messages is configured
                get_logger(CPP_LOGGER_NAME)

                _stop_cpp_log_retrieval_thread.clear()
                _cpp_log_retrieval_thread = threading.Thread(
                    target=_cpp_log_retrieval_worker,
                    name="CppLogRetrievalThread",
                    daemon=True
                )
                _cpp_log_retrieval_thread.start()
                MAIN_LOGGER.info("C++ log forwarding initialized and retrieval thread started.")
            except Exception as e:
                MAIN_LOGGER.error(f"Failed to initialize C++ log forwarding: {e}", exc_info=True)
        else:
            MAIN_LOGGER.info("C++ log retrieval thread already running.")
    else:
        MAIN_LOGGER.warning("C++ audio engine module not available. Cannot initialize C++ log forwarding.")

def _cleanup_cpp_log_forwarding():
    """Gracefully stops the C++ log retrieval thread and C++ logger."""
    MAIN_LOGGER.info("Attempting to cleanup C++ log forwarding...")

    if screamrouter_audio_engine:
        try:
            MAIN_LOGGER.info("Signaling C++ logger to shutdown...")
            screamrouter_audio_engine.shutdown_cpp_logger()
        except Exception as e:
            MAIN_LOGGER.error(f"Error signaling C++ logger to shutdown: {e}", exc_info=True)
    
    if _cpp_log_retrieval_thread is not None:
        MAIN_LOGGER.info("Signaling C++ log retrieval thread to stop...")
        _stop_cpp_log_retrieval_thread.set()
        
        if _cpp_log_retrieval_thread.is_alive():
            _cpp_log_retrieval_thread.join(timeout=2.0) # Wait for thread to finish

        if _cpp_log_retrieval_thread.is_alive():
            MAIN_LOGGER.warning("C++ log retrieval thread did not stop in time.")
        else:
            MAIN_LOGGER.info("C++ log retrieval thread stopped.")
    
    # Perform a final drain after signaling C++ shutdown and Python thread stop
    if screamrouter_audio_engine:
        try:
            MAIN_LOGGER.info("Performing final drain of C++ log messages...")
            final_entries = screamrouter_audio_engine.get_cpp_log_messages(timeout_ms=50) # Short timeout
            logger = logging.getLogger(CPP_LOGGER_NAME)
            for entry_tuple in final_entries:
                if len(entry_tuple) == 4:
                    cpp_level, message, cpp_filename, cpp_lineno = entry_tuple
                    python_level = _map_cpp_level_to_python(cpp_level)
                    record = logging.LogRecord(
                        name=CPP_LOGGER_NAME, level=python_level, pathname=cpp_filename,
                        lineno=cpp_lineno, msg=message, args=(), exc_info=None, func="N/A_CPP_FINAL_DRAIN")
                    logger.handle(record)
            MAIN_LOGGER.info(f"Drained {len(final_entries)} final C++ log messages.")
        except Exception as e:
            MAIN_LOGGER.error(f"Error during final drain of C++ log messages: {e}", exc_info=True)
    
    MAIN_LOGGER.info("C++ log forwarding cleanup finished.")


# Initialize C++ log forwarding when this module is imported.
if __name__ != "__main__":
    initialize_cpp_log_forwarding()
    atexit.register(_cleanup_cpp_log_forwarding)
