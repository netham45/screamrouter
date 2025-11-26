# Logger Binding (`src/audio_engine/utils/cpp_logger.h`)

Exposes C++ logging to Python via polling APIs.

## Bound symbols
- `LogLevel_CPP` enum: `DEBUG`, `INFO`, `WARNING`, `ERROR`.
- `get_cpp_log_messages(timeout_ms: int = 100) -> list[tuple[LogLevel_CPP, str, str, int]]`  
  Blocks up to `timeout_ms` waiting for C++ log entries; each tuple is `(level, message, filename, line)`.
- `shutdown_cpp_logger()`  
  Unblocks any active `get_cpp_log_messages` calls and signals logger shutdown.

## Usage notes
- This is **pull-based**: C++ does not push into Python. Run a background thread to poll and forward to Python logging (see `screamrouter/screamrouter_logger/screamrouter_logger.py`).
- Call `shutdown_cpp_logger()` during process teardown before joining the poll thread to avoid hang on exit.
- Log level mapping is 1:1 with C++ logger levels; map them to Python logging appropriately.
