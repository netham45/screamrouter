/**
 * @file thread_priority.h
 * @brief Helpers for elevating audio threads to real-time priority where supported.
 */
#ifndef SCREAMROUTER_AUDIO_THREAD_PRIORITY_H
#define SCREAMROUTER_AUDIO_THREAD_PRIORITY_H

#include <thread>

namespace screamrouter {
namespace audio {
namespace utils {

/**
 * @brief Promote the calling thread to real-time priority if the platform allows it.
 * @param thread_name Human-readable thread label for logging.
 * @return true on success, false if the promotion failed or is unsupported.
 */
bool set_current_thread_realtime_priority(const char* thread_name);

/**
 * @brief Promote a std::thread instance to real-time priority if the platform allows it.
 * @param thread Reference to the target thread.
 * @param thread_name Human-readable thread label for logging.
 * @return true on success, false if the promotion failed or is unsupported.
 */
bool set_thread_realtime_priority(std::thread& thread, const char* thread_name);

} // namespace utils
} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_THREAD_PRIORITY_H
