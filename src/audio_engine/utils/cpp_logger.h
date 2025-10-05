/**
 * @file cpp_logger.h
 * @brief Defines a logging framework for the C++ audio engine.
 * @details This file provides a simple, thread-safe logging mechanism that queues log
 *          messages from C++ and allows them to be retrieved by a Python layer.
 *          It includes log levels, a log entry structure, and macros for easy logging.
 */
#ifndef CPP_LOGGER_H
#define CPP_LOGGER_H

#include <string>
#include <functional>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

namespace screamrouter {
namespace audio {
namespace logging {

/**
 * @enum LogLevel
 * @brief Defines the severity levels for log messages.
 */
enum class LogLevel {
    DEBUG,   ///< Detailed information, typically of interest only when diagnosing problems.
    INFO,    ///< Confirmation that things are working as expected.
    WARNING, ///< An indication that something unexpected happened, or a potential problem.
    ERR      ///< A serious problem, preventing the program from performing a function.
};

/** @brief Global atomic variable to hold the current log level. */
extern std::atomic<LogLevel> current_log_level;

/**
 * @struct LogEntry
 * @brief Represents a single log message.
 */
struct LogEntry {
    LogLevel level;         ///< The severity level of the log message.
    std::string message;    ///< The log message content.
    std::string filename;   ///< The source file where the log was generated.
    int line_number;        ///< The line number in the source file.
};

/**
 * @brief Retrieves all currently buffered log entries.
 * @details This function is intended to be called by Python. It blocks until messages
 *          are available or a timeout occurs, then returns all entries currently in the
 *          queue and clears it.
 * @param timeout_ms The maximum time to wait in milliseconds.
 * @return A vector of `LogEntry` objects.
 */
std::vector<LogEntry> retrieve_log_entries(int timeout_ms = 100);

/**
 * @brief Signals the C++ logger to prepare for shutdown.
 * @details This will unblock any threads waiting on `retrieve_log_entries`.
 */
void shutdown_cpp_logger();

/**
 * @brief Sets the global C++ log level.
 * @param level The new log level to set. Messages below this level will be ignored.
 */
void set_cpp_log_level(LogLevel level);

/**
 * @brief Dispatches a log message to the internal C++ queue.
 * @details This function handles printf-style formatting and captures file/line info.
 * @param level The log level.
 * @param file The source file name (`__FILE__`).
 * @param line The source line number (`__LINE__`).
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 */
void log_message(LogLevel level, const char* file, int line, const char* format, ...);

/**
 * @brief Helper function to extract the base filename from a full path.
 * @param path The full path to the file.
 * @return A pointer to the base filename within the path string.
 */
const char* get_base_filename(const char* path);
 
/**
 * @brief Binds the C++ logging components to a Python module.
 * @param m The pybind11 module to which the components will be bound.
 */
inline void bind_logger(pybind11::module_ &m) {
    namespace py = pybind11;
    py::enum_<LogLevel>(m, "LogLevel_CPP")
        .value("DEBUG", LogLevel::DEBUG)
        .value("INFO", LogLevel::INFO)
        .value("WARNING", LogLevel::WARNING)
        .value("ERROR", LogLevel::ERR)
        .export_values();
 
    m.def("get_cpp_log_messages", [](int timeout_ms) {
        std::vector<std::tuple<screamrouter::audio::logging::LogLevel, std::string, std::string, int>> entries_tuples;
        std::vector<screamrouter::audio::logging::LogEntry> cpp_entries;
            
        {
            py::gil_scoped_release release_gil;
            cpp_entries = screamrouter::audio::logging::retrieve_log_entries(timeout_ms);
        }
 
        for (const auto& entry : cpp_entries) {
            entries_tuples.emplace_back(entry.level, entry.message, entry.filename, entry.line_number);
        }
        return entries_tuples;
    }, py::arg("timeout_ms") = 100,
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

/**
 * @def LOG_CPP_BASE
 * @brief A base macro for logging. Not intended for direct use.
 */
#define LOG_CPP_BASE(level, fmt, ...) \
    screamrouter::audio::logging::log_message( \
        level, \
        screamrouter::audio::logging::get_base_filename(__FILE__), \
        __LINE__, \
        fmt, \
        ##__VA_ARGS__)

/** @def LOG_CPP_DEBUG(fmt, ...) @brief Logs a message at the DEBUG level. */
#define LOG_CPP_DEBUG(fmt, ...)   LOG_CPP_BASE(screamrouter::audio::logging::LogLevel::DEBUG, fmt, ##__VA_ARGS__)
/** @def LOG_CPP_INFO(fmt, ...) @brief Logs a message at the INFO level. */
#define LOG_CPP_INFO(fmt, ...)    LOG_CPP_BASE(screamrouter::audio::logging::LogLevel::INFO, fmt, ##__VA_ARGS__)
/** @def LOG_CPP_WARNING(fmt, ...) @brief Logs a message at the WARNING level. */
#define LOG_CPP_WARNING(fmt, ...) LOG_CPP_BASE(screamrouter::audio::logging::LogLevel::WARNING, fmt, ##__VA_ARGS__)
/** @def LOG_CPP_ERROR(fmt, ...) @brief Logs a message at the ERROR level. */
#define LOG_CPP_ERROR(fmt, ...)   LOG_CPP_BASE(screamrouter::audio::logging::LogLevel::ERR, fmt, ##__VA_ARGS__)

#endif // CPP_LOGGER_H