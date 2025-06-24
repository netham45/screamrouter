#ifndef CPP_LOGGER_H
#define CPP_LOGGER_H

#include <string>
#include <functional> // Keep for LogLevel, or remove if LogLevel is self-contained
#include <vector>   // For returning a batch of logs
#include <deque>    // For the internal queue
#include <mutex>    // For protecting the queue
#include <atomic>   // For std::atomic
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
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

// Global variable to hold the current log level, initialized to INFO
extern std::atomic<LogLevel> current_log_level;

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
 * @brief Sets the global C++ log level.
 * @param level The new log level to set.
 */
void set_cpp_log_level(LogLevel level);

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
 
inline void bind_logger(pybind11::module_ &m) {
    namespace py = pybind11;
    py::enum_<LogLevel>(m, "LogLevel_CPP") // Suffix with _CPP to avoid potential name clash if Python has LogLevel
        .value("DEBUG", LogLevel::DEBUG)
        .value("INFO", LogLevel::INFO)
        .value("WARNING", LogLevel::WARNING)
        .value("ERROR", LogLevel::ERROR)
        .export_values();
 
    m.def("get_cpp_log_messages", [](int timeout_ms) {
        std::vector<std::tuple<screamrouter::audio::logging::LogLevel, std::string, std::string, int>> entries_tuples;
        std::vector<screamrouter::audio::logging::LogEntry> cpp_entries;
            
        // Release GIL while C++ function blocks
        {
            py::gil_scoped_release release_gil; // Release GIL
            cpp_entries = screamrouter::audio::logging::retrieve_log_entries(timeout_ms);
            // GIL is re-acquired automatically when release_gil goes out of scope
        }
 
        for (const auto& entry : cpp_entries) {
            entries_tuples.emplace_back(entry.level, entry.message, entry.filename, entry.line_number);
        }
        return entries_tuples; // pybind11 converts this to Python list of tuples
    }, py::arg("timeout_ms") = 100, // Default timeout if Python doesn't specify
       "Retrieves buffered C++ log messages, blocking until messages are available or timeout occurs (in ms). Returns a list of (level, message, filename, line) tuples.");
 
    m.def("shutdown_cpp_logger", &screamrouter::audio::logging::shutdown_cpp_logger,
          "Signals the C++ logger to prepare for shutdown, unblocking any waiting log retrieval calls.");
 
    m.def("set_cpp_log_level", &screamrouter::audio::logging::set_cpp_log_level,
          py::arg("level"),
          "Sets the C++ global log level.");
}
 
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