#include "cpp_logger.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdarg> // For va_list, va_start, va_end
#include <cstdio>  // For vsnprintf
#include <cstring> // For strrchr

#include <chrono> // For std::chrono::milliseconds
#include <algorithm> // For std::min
#include <condition_variable> // For std::condition_variable

namespace screamrouter {
namespace audio {
namespace logging {

namespace { // Anonymous namespace for internal linkage
    // LogCallback global_log_callback; // Removed
    // std::mutex callback_mutex; // Replaced with queue-specific mutex

    std::deque<screamrouter::audio::logging::LogEntry> internal_log_queue;
    std::mutex internal_log_queue_mutex;
    std::condition_variable internal_log_queue_cv;
    const size_t MAX_LOG_QUEUE_SIZE = 2048; // Max number of log entries in queue
    bool shutdown_requested = false;
    bool overflow_message_logged_since_clear = false; // To prevent spamming overflow messages
}

// void set_log_callback(LogCallback callback) { // Removed
//     std::lock_guard<std::mutex> lock(callback_mutex);
//     global_log_callback = callback;
// }

const char* get_base_filename(const char* path) {
    if (!path) {
        return "";
    }
    const char* last_slash = strrchr(path, '/');
    const char* last_backslash = strrchr(path, '\\');
    
    const char* base = nullptr;
    if (last_slash && last_backslash) {
        base = (last_slash > last_backslash) ? last_slash + 1 : last_backslash + 1;
    } else if (last_slash) {
        base = last_slash + 1;
    } else if (last_backslash) {
        base = last_backslash + 1;
    } else {
        base = path; // No slashes, path is filename
    }
    return base;
}

void log_message(LogLevel level, const char* file, int line, const char* format, ...) {
    // Format the message
    std::vector<char> buffer(1024);
    va_list args;
    va_start(args, format);
    int needed = vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    if (needed < 0) {
        // Consider logging this formatting error to std::cerr directly,
        // as it's an issue before even attempting to queue.
        std::cerr << "CppLogger: Encoding error in log_message for file " << (file ? file : "unknown_file") << ":" << line << std::endl;
        return;
    }

    if (static_cast<size_t>(needed) >= buffer.size()) {
        buffer.resize(static_cast<size_t>(needed) + 1);
        va_start(args, format);
        vsnprintf(buffer.data(), buffer.size(), format, args);
        va_end(args);
    }

    LogEntry new_entry;
    new_entry.level = level;
    new_entry.message = std::string(buffer.data());
    new_entry.filename = (file ? std::string(file) : "unknown_file"); // Store copy
    new_entry.line_number = line;

    { // Scope for the lock
        std::unique_lock<std::mutex> lock(internal_log_queue_mutex);
        if (shutdown_requested) {
            return; // Don't add new logs if shutdown is in progress
        }

        if (internal_log_queue.size() >= MAX_LOG_QUEUE_SIZE) {
            internal_log_queue.pop_front(); // Drop oldest
            if (!overflow_message_logged_since_clear) {
                 // To avoid spamming cerr, only log overflow once until queue is cleared by Python
                 // Or, add a special LogEntry indicating overflow. For now, simple cerr message.
                LogEntry overflow_entry;
                overflow_entry.level = LogLevel::WARNING;
                overflow_entry.message = "C++ log queue overflow. Oldest messages dropped.";
                overflow_entry.filename = "cpp_logger.cpp";
                overflow_entry.line_number = __LINE__; // Approximate line
                internal_log_queue.push_back(std::move(overflow_entry)); // Add this as a log entry itself
                overflow_message_logged_since_clear = true; // Mark that we've logged an overflow
                // Alternatively, a simple std::cerr message:
                // std::cerr << "CppLogger [WARNING][cpp_logger.cpp]: C++ log queue overflow. Oldest messages dropped." << std::endl;
                // overflow_message_logged_since_clear = true; // If using cerr directly
            }
        }
        internal_log_queue.push_back(std::move(new_entry));
        lock.unlock(); // Unlock before notifying
        internal_log_queue_cv.notify_one(); // Notify one waiting thread (Python retriever)
    }
}

std::vector<LogEntry> retrieve_log_entries(int timeout_ms) {
    std::vector<LogEntry> batch;
    std::unique_lock<std::mutex> lock(internal_log_queue_mutex);

    if (!internal_log_queue_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                         []{ return !internal_log_queue.empty() || shutdown_requested; })) {
        // Timeout occurred, queue is still empty, and no shutdown
        return batch; // Return empty vector
    }

    // If woken up, either queue is not empty OR shutdown is requested.
    if (shutdown_requested && internal_log_queue.empty()) {
        return batch; // Shutdown and queue is empty, nothing to retrieve
    }
    
    // Determine how many items to grab.
    // Example: grab all available up to a certain reasonable batch size (e.g., 100 or 200)
    // to avoid holding the lock for too long if the queue is massive.
    size_t items_to_grab = std::min(internal_log_queue.size(), static_cast<size_t>(100));
    batch.reserve(items_to_grab);

    for (size_t i = 0; i < items_to_grab && !internal_log_queue.empty(); ++i) {
        batch.push_back(std::move(internal_log_queue.front()));
        internal_log_queue.pop_front();
    }
    
    // If we cleared the queue sufficiently, reset the overflow message flag
    if (overflow_message_logged_since_clear && internal_log_queue.size() < (MAX_LOG_QUEUE_SIZE / 2)) {
        overflow_message_logged_since_clear = false;
    }

    return batch;
}

void shutdown_cpp_logger() {
    std::unique_lock<std::mutex> lock(internal_log_queue_mutex);
    shutdown_requested = true;
    lock.unlock();
    internal_log_queue_cv.notify_all(); // Wake up any waiting retrieve_log_entries calls
}

} // namespace logging
} // namespace audio
} // namespace screamrouter