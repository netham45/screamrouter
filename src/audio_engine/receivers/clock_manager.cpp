#include "clock_manager.h"

#include <algorithm>
#include <cerrno>
#include <optional>
#include <stdexcept>

#if defined(__linux__)
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace screamrouter {
namespace audio {

class ClockManager::PlatformTimer {
public:
    virtual ~PlatformTimer() = default;
    virtual bool is_valid() const = 0;
    virtual bool arm(std::optional<std::chrono::steady_clock::time_point> deadline) = 0;
    virtual ClockManager::TimerWaitResult wait() = 0;
    virtual void notify() = 0;
};

namespace {
constexpr std::chrono::nanoseconds kMinimumPeriod{1};

class ClockManagerPlatformTimerBase : public ClockManager::PlatformTimer {
public:
    ~ClockManagerPlatformTimerBase() override = default;
};

#if defined(__linux__)

timespec to_timespec(std::chrono::nanoseconds duration) {
    if (duration.count() < 0) {
        duration = std::chrono::nanoseconds::zero();
    }
    timespec ts;
    ts.tv_sec = static_cast<time_t>(duration.count() / 1'000'000'000);
    ts.tv_nsec = static_cast<long>(duration.count() % 1'000'000'000);
    return ts;
}

class PosixPlatformTimer final : public ClockManagerPlatformTimerBase {
public:
    PosixPlatformTimer() {
        timer_fd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        if (timer_fd_ < 0) {
            return;
        }
        wake_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (wake_fd_ < 0) {
            ::close(timer_fd_);
            timer_fd_ = -1;
        }
    }

    ~PosixPlatformTimer() override {
        if (timer_fd_ >= 0) {
            ::close(timer_fd_);
        }
        if (wake_fd_ >= 0) {
            ::close(wake_fd_);
        }
    }

    bool is_valid() const override {
        return timer_fd_ >= 0 && wake_fd_ >= 0;
    }

    bool arm(std::optional<std::chrono::steady_clock::time_point> deadline) override {
        if (!is_valid()) {
            return false;
        }

        itimerspec spec{};
        if (deadline.has_value()) {
            auto now = std::chrono::steady_clock::now();
            auto rel = deadline.value() - now;
            if (rel <= std::chrono::steady_clock::duration::zero()) {
                rel = std::chrono::nanoseconds(1);
            }
            spec.it_value = to_timespec(std::chrono::duration_cast<std::chrono::nanoseconds>(rel));
        } else {
            spec.it_value = {};
        }

        return ::timerfd_settime(timer_fd_, 0, &spec, nullptr) == 0;
    }

    ClockManager::TimerWaitResult wait() override {
        if (!is_valid()) {
            return ClockManager::TimerWaitResult::Notified;
        }

        pollfd fds[2];
        fds[0].fd = timer_fd_;
        fds[0].events = POLLIN;
        fds[1].fd = wake_fd_;
        fds[1].events = POLLIN;

        for (;;) {
            int ret = ::poll(fds, 2, -1);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return ClockManager::TimerWaitResult::Notified;
            }

            if (fds[1].revents & POLLIN) {
                drain_eventfd();
                return ClockManager::TimerWaitResult::Notified;
            }

            if (fds[0].revents & POLLIN) {
                drain_timerfd();
                return ClockManager::TimerWaitResult::TimerFired;
            }
        }
    }

    void notify() override {
        if (wake_fd_ < 0) {
            return;
        }
        const uint64_t one = 1;
        ssize_t unused = ::write(wake_fd_, &one, sizeof(one));
        (void)unused;
    }

private:
    void drain_eventfd() const {
        if (wake_fd_ < 0) {
            return;
        }

        uint64_t value = 0;
        while (true) {
            ssize_t result = ::read(wake_fd_, &value, sizeof(value));
            if (result <= 0) {
                if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    break;
                }
                break;
            }
            if (value == 0) {
                break;
            }
        }
    }

    void drain_timerfd() const {
        if (timer_fd_ < 0) {
            return;
        }
        uint64_t expirations = 0;
        ssize_t result = ::read(timer_fd_, &expirations, sizeof(expirations));
        (void)result;
    }

    int timer_fd_{-1};
    int wake_fd_{-1};
};

#elif defined(_WIN32)

LARGE_INTEGER to_due_time(std::chrono::steady_clock::time_point deadline) {
    auto now = std::chrono::steady_clock::now();
    auto diff = deadline - now;
    LARGE_INTEGER li{};
    if (diff <= std::chrono::steady_clock::duration::zero()) {
        li.QuadPart = 0;
    } else {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count();
        auto hundred_ns = ns / 100;
        if (ns % 100 != 0) {
            ++hundred_ns;
        }
        if (hundred_ns <= 0) {
            hundred_ns = 1;
        }
        li.QuadPart = -static_cast<LONGLONG>(hundred_ns);
    }
    return li;
}

class WinPlatformTimer final : public ClockManagerPlatformTimerBase {
public:
    WinPlatformTimer() {
#if defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
        timer_handle_ = CreateWaitableTimerExW(
            nullptr,
            nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS);
#else
        timer_handle_ = nullptr;
#endif
        if (!timer_handle_) {
            timer_handle_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        }
        wake_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    ~WinPlatformTimer() override {
        if (timer_handle_) {
            CancelWaitableTimer(timer_handle_);
            CloseHandle(timer_handle_);
        }
        if (wake_event_) {
            CloseHandle(wake_event_);
        }
    }

    bool is_valid() const override {
        return timer_handle_ != nullptr && wake_event_ != nullptr;
    }

    bool arm(std::optional<std::chrono::steady_clock::time_point> deadline) override {
        if (!is_valid()) {
            return false;
        }

        if (!deadline.has_value()) {
            return CancelWaitableTimer(timer_handle_) != 0;
        }

        LARGE_INTEGER due_time = to_due_time(deadline.value());
        return SetWaitableTimer(
                   timer_handle_,
                   &due_time,
                   0,
                   nullptr,
                   nullptr,
                   FALSE) != 0;
    }

    ClockManager::TimerWaitResult wait() override {
        if (!is_valid()) {
            return ClockManager::TimerWaitResult::Notified;
        }

        HANDLE handles[2] = {timer_handle_, wake_event_};
        for (;;) {
            DWORD status = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (status == WAIT_OBJECT_0) {
                return ClockManager::TimerWaitResult::TimerFired;
            }
            if (status == WAIT_OBJECT_0 + 1) {
                ResetEvent(wake_event_);
                return ClockManager::TimerWaitResult::Notified;
            }
            if (status == WAIT_FAILED) {
                return ClockManager::TimerWaitResult::Notified;
            }
        }
    }

    void notify() override {
        if (!wake_event_) {
            return;
        }
        SetEvent(wake_event_);
    }

private:
    HANDLE timer_handle_{nullptr};
    HANDLE wake_event_{nullptr};
};

#endif

std::unique_ptr<ClockManager::PlatformTimer> create_platform_timer() {
#if defined(__linux__)
    auto timer = std::make_unique<PosixPlatformTimer>();
    if (!timer->is_valid()) {
        return nullptr;
    }
    return timer;
#elif defined(_WIN32)
    auto timer = std::make_unique<WinPlatformTimer>();
    if (!timer->is_valid()) {
        return nullptr;
    }
    return timer;
#else
    return nullptr;
#endif
}
}

ClockManager::ClockManager(std::size_t chunk_size_bytes)
    : platform_timer_(create_platform_timer()),
      chunk_size_bytes_(sanitize_chunk_size_bytes(chunk_size_bytes)) {
    worker_thread_ = std::thread([this]() { run(); });
}

ClockManager::~ClockManager() {
    stop_requested_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (platform_timer_) {
            platform_timer_->notify();
        }
    }
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

std::chrono::nanoseconds ClockManager::calculate_period(int sample_rate, int channels, int bit_depth) const {
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

    const long double seconds = static_cast<long double>(chunk_size_bytes_) /
                                static_cast<long double>(bytes_per_second);
    auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<long double>(seconds));

    if (period <= kMinimumPeriod) {
        period = kMinimumPeriod;
    }

    return period;
}

bool ClockManager::has_active_conditions(const ClockEntry& entry) const {
    return std::any_of(entry.conditions.begin(), entry.conditions.end(), [](const auto& cond) {
        if (!cond) {
            return false;
        }
        if (!cond->active.load(std::memory_order_acquire)) {
            return false;
        }
        return !cond->condition.expired();
    });
}

void ClockManager::cleanup_inactive_conditions(ClockEntry& entry) {
    entry.conditions.erase(
        std::remove_if(entry.conditions.begin(), entry.conditions.end(), [](const auto& cond) {
            if (!cond) {
                return true;
            }
            if (!cond->active.load(std::memory_order_acquire)) {
                return true;
            }
            return cond->condition.expired();
        }),
        entry.conditions.end());
}

void ClockManager::run() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (!stop_requested_.load(std::memory_order_acquire)) {
        if (clock_entries_.empty()) {
            if (platform_timer_) {
                bool arm_failed = false;
                TimerWaitResult wait_result = TimerWaitResult::Notified;
                lock.unlock();
                if (!platform_timer_->arm(std::nullopt)) {
                    arm_failed = true;
                } else {
                    wait_result = platform_timer_->wait();
                }
                lock.lock();

                if (arm_failed) {
                    platform_timer_.reset();
                    continue;
                }

                if (stop_requested_.load(std::memory_order_acquire)) {
                    break;
                }

                if (wait_result == TimerWaitResult::Stopped) {
                    break;
                }

                continue;
            }

            cv_.wait(lock, [this]() {
                return stop_requested_.load(std::memory_order_acquire) || !clock_entries_.empty();
            });
            continue;
        }

        ClockKey next_key{};
        std::chrono::steady_clock::time_point next_fire_time{};
        bool have_next = false;

        for (auto it = clock_entries_.begin(); it != clock_entries_.end();) {
            cleanup_inactive_conditions(it->second);
            if (it->second.conditions.empty()) {
                it = clock_entries_.erase(it);
                continue;
            }

            if (!has_active_conditions(it->second)) {
                ++it;
                continue;
            }

            if (it->second.next_fire.time_since_epoch().count() == 0) {
                it->second.next_fire = std::chrono::steady_clock::now() + it->second.period;
            }

            if (!have_next || it->second.next_fire < next_fire_time) {
                next_key = it->first;
                next_fire_time = it->second.next_fire;
                have_next = true;
            }
            ++it;
        }

        if (!have_next) {
            if (platform_timer_) {
                bool arm_failed = false;
                TimerWaitResult wait_result = TimerWaitResult::Notified;
                lock.unlock();
                if (!platform_timer_->arm(std::nullopt)) {
                    arm_failed = true;
                } else {
                    wait_result = platform_timer_->wait();
                }
                lock.lock();

                if (arm_failed) {
                    platform_timer_.reset();
                    continue;
                }

                if (stop_requested_.load(std::memory_order_acquire)) {
                    break;
                }

                if (wait_result == TimerWaitResult::Stopped) {
                    break;
                }

            } else {
                cv_.wait(lock, [this]() {
                    return stop_requested_.load(std::memory_order_acquire) || !clock_entries_.empty();
                });
            }
            continue;
        }

        bool timer_fired = false;
        if (platform_timer_) {
            bool arm_failed = false;
            TimerWaitResult wait_result = TimerWaitResult::Notified;
            lock.unlock();
            if (!platform_timer_->arm(next_fire_time)) {
                arm_failed = true;
            } else {
                wait_result = platform_timer_->wait();
            }
            lock.lock();

            if (arm_failed) {
                platform_timer_.reset();
                continue;
            }

            if (stop_requested_.load(std::memory_order_acquire)) {
                break;
            }

            if (wait_result == TimerWaitResult::TimerFired) {
                timer_fired = true;
            } else if (wait_result == TimerWaitResult::Stopped) {
                break;
            } else {
                continue;
            }
        } else {
            auto status = cv_.wait_until(lock, next_fire_time);
            if (stop_requested_.load(std::memory_order_acquire)) {
                break;
            }

            if (status == std::cv_status::timeout) {
                timer_fired = true;
            } else {
                continue;
            }
        }

        if (!timer_fired) {
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        auto entry_it = clock_entries_.find(next_key);
        if (entry_it == clock_entries_.end()) {
            continue;
        }

        auto& entry = entry_it->second;
        if (!has_active_conditions(entry)) {
            continue;
        }

        auto conditions = entry.conditions;
        const auto period = entry.period;
        entry.next_fire += period;
        while (entry.next_fire <= now) {
            entry.next_fire += period;
        }

        lock.unlock();
        for (const auto& cond_entry : conditions) {
            if (stop_requested_.load(std::memory_order_acquire)) {
                break;
            }
            if (!cond_entry || !cond_entry->active.load(std::memory_order_acquire)) {
                continue;
            }
            if (auto condition_state = cond_entry->condition.lock()) {
                {
                    std::unique_lock<std::mutex> condition_lock(condition_state->mutex);
                    condition_state->sequence++;
                }
                condition_state->cv.notify_all();
            } else {
                cond_entry->active.store(false, std::memory_order_release);
            }
        }
        lock.lock();

        entry_it = clock_entries_.find(next_key);
        if (entry_it != clock_entries_.end()) {
            cleanup_inactive_conditions(entry_it->second);
            if (entry_it->second.conditions.empty()) {
                clock_entries_.erase(entry_it);
            }
        }
    }
}

ClockManager::ConditionHandle ClockManager::register_clock_condition(
    int sample_rate,
    int channels,
    int bit_depth) {

    auto period = calculate_period(sample_rate, channels, bit_depth);
    auto condition = std::make_shared<ClockCondition>();
    auto condition_entry = std::make_shared<ConditionEntry>();
    auto condition_id = next_condition_id_.fetch_add(1, std::memory_order_relaxed);
    condition_entry->id = condition_id;
    condition_entry->condition = condition;
    condition_entry->active.store(true, std::memory_order_release);
    condition_entry->key = ClockKey{sample_rate, channels, bit_depth};

    ClockKey key{sample_rate, channels, bit_depth};

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& entry = clock_entries_[key];
        if (entry.period.count() == 0) {
            entry.period = period;
            entry.next_fire = std::chrono::steady_clock::now() + period;
        }
        entry.conditions.push_back(std::move(condition_entry));
        if (platform_timer_) {
            platform_timer_->notify();
        }
    }

    cv_.notify_all();

    ConditionHandle handle;
    handle.key = key;
    handle.id = condition_id;
    handle.condition = std::move(condition);
    return handle;
}

void ClockManager::unregister_clock_condition(const ConditionHandle& handle) {
    if (!handle.valid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clock_entries_.find(handle.key);
    if (it == clock_entries_.end()) {
        return;
    }

    auto& entry = it->second;
    bool modified = false;
    for (auto& cond_entry : entry.conditions) {
        if (cond_entry && cond_entry->id == handle.id) {
            cond_entry->active.store(false, std::memory_order_release);
            modified = true;
        }
    }

    if (modified) {
        cleanup_inactive_conditions(entry);
        if (entry.conditions.empty()) {
            clock_entries_.erase(it);
        }
    }

    if (platform_timer_) {
        platform_timer_->notify();
    }
    cv_.notify_all();
}

} // namespace audio
} // namespace screamrouter
