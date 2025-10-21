#include "clock_manager.h"

#include <algorithm>
#include <stdexcept>

namespace screamrouter {
namespace audio {

namespace {
constexpr std::chrono::nanoseconds kMinimumPeriod{1};
}

ClockManager::ClockManager() {
    worker_thread_ = std::thread([this]() { run(); });
}

ClockManager::~ClockManager() {
    stop_requested_.store(true, std::memory_order_release);
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

ClockManager::CallbackId ClockManager::register_clock(
    int sample_rate,
    int channels,
    int bit_depth,
    Callback callback) {

    if (!callback) {
        throw std::invalid_argument("ClockManager::register_clock requires a valid callback");
    }

    auto period = calculate_period(sample_rate, channels, bit_depth);
    auto callback_entry = std::make_shared<CallbackEntry>();
    auto callback_id = next_callback_id_.fetch_add(1, std::memory_order_relaxed);
    callback_entry->id = callback_id;
    callback_entry->callback = std::move(callback);
    callback_entry->active.store(true, std::memory_order_release);

    ClockKey key{sample_rate, channels, bit_depth};

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& entry = clock_entries_[key];
        if (entry.period.count() == 0) {
            entry.period = period;
            entry.next_fire = std::chrono::steady_clock::now() + period;
        }
        entry.callbacks.push_back(std::move(callback_entry));
    }

    cv_.notify_all();
    return callback_id;
}

void ClockManager::unregister_clock(int sample_rate, int channels, int bit_depth, CallbackId callback_id) {
    ClockKey key{sample_rate, channels, bit_depth};

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clock_entries_.find(key);
        if (it == clock_entries_.end()) {
            return;
        }

        auto& entry = it->second;
        bool modified = false;
        for (auto& cb : entry.callbacks) {
            if (cb && cb->id == callback_id) {
                cb->active.store(false, std::memory_order_release);
                modified = true;
            }
        }

        if (modified) {
            cleanup_inactive_callbacks(entry);
            if (entry.callbacks.empty()) {
                clock_entries_.erase(it);
            }
        }
    }

    cv_.notify_all();
}

std::chrono::nanoseconds ClockManager::calculate_period(int sample_rate, int channels, int bit_depth) {
    if (sample_rate <= 0) {
        throw std::invalid_argument("ClockManager requires sample_rate > 0");
    }
    if (channels <= 0) {
        throw std::invalid_argument("ClockManager requires channels > 0");
    }
    if (bit_depth <= 0 || (bit_depth % 8) != 0) {
        throw std::invalid_argument("ClockManager requires bit_depth to be a positive multiple of 8");
    }

    const std::size_t bytes_per_channel_sample = static_cast<std::size_t>(bit_depth) / 8;
    const std::size_t frame_bytes = bytes_per_channel_sample * static_cast<std::size_t>(channels);
    if (frame_bytes == 0) {
        throw std::invalid_argument("ClockManager calculated zero-sized audio frame");
    }

    const std::size_t bytes_per_second = frame_bytes * static_cast<std::size_t>(sample_rate);
    if (bytes_per_second == 0) {
        throw std::invalid_argument("ClockManager calculated zero bytes-per-second");
    }

    const long double seconds = static_cast<long double>(kChunkSizeBytes) /
                                static_cast<long double>(bytes_per_second);
    auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<long double>(seconds));

    if (period <= kMinimumPeriod) {
        period = kMinimumPeriod;
    }

    return period;
}

bool ClockManager::has_active_callbacks(const ClockEntry& entry) const {
    return std::any_of(entry.callbacks.begin(), entry.callbacks.end(), [](const auto& cb) {
        return cb && cb->active.load(std::memory_order_acquire);
    });
}

void ClockManager::cleanup_inactive_callbacks(ClockEntry& entry) {
    entry.callbacks.erase(
        std::remove_if(entry.callbacks.begin(), entry.callbacks.end(), [](const auto& cb) {
            return !cb || !cb->active.load(std::memory_order_acquire);
        }),
        entry.callbacks.end());
}

void ClockManager::run() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (!stop_requested_.load(std::memory_order_acquire)) {
        if (clock_entries_.empty()) {
            cv_.wait(lock, [this]() {
                return stop_requested_.load(std::memory_order_acquire) || !clock_entries_.empty();
            });
            continue;
        }

        ClockKey next_key{};
        std::chrono::steady_clock::time_point next_fire_time{};
        bool have_next = false;

        for (auto it = clock_entries_.begin(); it != clock_entries_.end();) {
            cleanup_inactive_callbacks(it->second);
            if (it->second.callbacks.empty()) {
                it = clock_entries_.erase(it);
                continue;
            }

            if (!has_active_callbacks(it->second)) {
                ++it;
                continue;
            }

            if (!have_next || it->second.next_fire < next_fire_time) {
                next_key = it->first;
                next_fire_time = it->second.next_fire;
                have_next = true;
            }
            ++it;
        }

        if (!have_next) {
            cv_.wait(lock, [this]() {
                return stop_requested_.load(std::memory_order_acquire) || !clock_entries_.empty();
            });
            continue;
        }

        auto status = cv_.wait_until(lock, next_fire_time);
        if (stop_requested_.load(std::memory_order_acquire)) {
            break;
        }

        if (status == std::cv_status::no_timeout) {
            // Woken by registration/unregistration or spurious wakeup; recompute schedule.
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        auto entry_it = clock_entries_.find(next_key);
        if (entry_it == clock_entries_.end()) {
            continue;
        }

        auto& entry = entry_it->second;
        if (!has_active_callbacks(entry)) {
            continue;
        }

        auto callbacks = entry.callbacks; // Copy shared_ptrs for invocation outside lock
        const auto period = entry.period;
        entry.next_fire += period;
        while (entry.next_fire <= now) {
            entry.next_fire += period;
        }

        lock.unlock();
        for (const auto& cb : callbacks) {
            if (stop_requested_.load(std::memory_order_acquire)) {
                break;
            }
            if (cb && cb->active.load(std::memory_order_acquire) && cb->callback) {
                cb->callback();
            }
        }
        lock.lock();

        entry_it = clock_entries_.find(next_key);
        if (entry_it != clock_entries_.end()) {
            cleanup_inactive_callbacks(entry_it->second);
            if (entry_it->second.callbacks.empty()) {
                clock_entries_.erase(entry_it);
            }
        }
    }
}

} // namespace audio
} // namespace screamrouter
