#pragma once

#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdint>

namespace screamrouter {
namespace audio_engine {

/**
 * Singleton class providing a single reference clock for all timing decisions.
 * Uses steady_clock as the monotonic base, with optional NTP offset tracking.
 */
class ReferenceClockManager {
public:
    // Singleton access
    static ReferenceClockManager& get_instance();
    
    // Get current reference time
    std::chrono::steady_clock::time_point now() const;
    
    // Convert between reference time and NTP timestamp (64-bit)
    uint64_t reference_time_to_ntp(std::chrono::steady_clock::time_point ref_time) const;
    std::chrono::steady_clock::time_point ntp_to_reference_time(uint64_t ntp_timestamp) const;
    
    // Register NTP synchronization source (for future use)
    void register_ntp_source(const std::string& source_id, 
                            uint64_t ntp_timestamp, 
                            std::chrono::steady_clock::time_point received_at);
    
    // Get current NTP offset
    std::chrono::nanoseconds get_ntp_offset() const;
    
    // Reset to default state (for testing)
    void reset();

private:
    ReferenceClockManager();
    ~ReferenceClockManager() = default;
    
    // Prevent copying
    ReferenceClockManager(const ReferenceClockManager&) = delete;
    ReferenceClockManager& operator=(const ReferenceClockManager&) = delete;
    
    // Reference epoch (when the manager was created)
    std::chrono::steady_clock::time_point m_reference_epoch;
    
    // Offset between local steady_clock and NTP time
    std::atomic<int64_t> m_ntp_offset_ns{0};
    
    // Mutex for NTP synchronization
    mutable std::mutex m_sync_mutex;
    
    // Helper: Convert steady_clock to NTP timestamp
    static uint64_t steady_clock_to_ntp(std::chrono::steady_clock::time_point tp, int64_t offset_ns);
    static std::chrono::steady_clock::time_point ntp_to_steady_clock(uint64_t ntp_ts, int64_t offset_ns);
};

} // namespace audio_engine
} // namespace screamrouter