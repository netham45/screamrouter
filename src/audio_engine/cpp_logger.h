#ifndef CPP_LOGGER_H
#define CPP_LOGGER_H

#include <string>
#include <functional> // Keep for LogLevel, or remove if LogLevel is self-contained
#include <vector>   // For returning a batch of logs
#include <deque>    // For the internal queue
#include <mutex>    // For protecting the queue

// Forward declaration for pybind11 object if used directly in callback type - No longer needed
// For now, using std::function which is more generic for C++ side. - No longer needed
// Python-specifics will be handled by the actual callback passed from Python. - No longer needed

namespace screamrouter {
namespace audio {
namespace logging {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

// Struct to hold log entry data
struct LogEntry {
    LogLevel level;
    std::string message;
    std::string filename; // Store as std::string to own the data
    int line_number;
    // Consider adding a timestamp if C++ side timestamping is desired
};

// LogCallback and set_log_callback are removed.

/**
 * @brief Retrieves all currently buffered log entries.
 * This function is intended to be called by Python via pybind11.
 * It clears the internal C++ queue after retrieving the entries.
 * It may block until messages are available or a timeout occurs.
 * @param timeout_ms The maximum time to wait in milliseconds.
 * @return A vector of LogEntry objects.
 */
std::vector<LogEntry> retrieve_log_entries(int timeout_ms = 100); // Add timeout_ms parameter

/**
 * @brief Signals the C++ logger to prepare for shutdown.
 * This will unblock any threads waiting on retrieve_log_entries.
 */
void shutdown_cpp_logger(); // Add declaration

/**
 * @brief Dispatches a log message to an internal C++ queue.
 * If a callback is set, it's invoked. Otherwise, logs to std::cerr. - Old comment, update
 * This function handles printf-style formatting.
 * This function handles printf-style formatting.
 * @param level The log level.
 * @param file The source file name (__FILE__).
 * @param line The source line number (__LINE__).
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 */
void log_message(LogLevel level, const char* file, int line, const char* format, ...);

// Helper to get base filename
const char* get_base_filename(const char* path);

} // namespace logging
} // namespace audio
} // namespace screamrouter

// Logging Macros
// __VA_ARGS__ requires C++11 or gnu extensions.
// Using a helper for basename extraction to keep macros cleaner.

#define LOG_CPP_BASE(level, fmt, ...) \
    screamrouter::audio::logging::log_message( \
        level, \
        screamrouter::audio::logging::get_base_filename(__FILE__), \
        __LINE__, \
        fmt, \
        ##__VA_ARGS__)

#define LOG_CPP_DEBUG(fmt, ...)   LOG_CPP_BASE(screamrouter::audio::logging::LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define LOG_CPP_INFO(fmt, ...)    LOG_CPP_BASE(screamrouter::audio::logging::LogLevel::INFO, fmt, ##__VA_ARGS__)
#define LOG_CPP_WARNING(fmt, ...) LOG_CPP_BASE(screamrouter::audio::logging::LogLevel::WARNING, fmt, ##__VA_ARGS__)
#define LOG_CPP_ERROR(fmt, ...)   LOG_CPP_BASE(screamrouter::audio::logging::LogLevel::ERROR, fmt, ##__VA_ARGS__)

#endif // CPP_LOGGER_H