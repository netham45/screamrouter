#include "reference_clock_manager.h"
#include <ctime>

namespace screamrouter {
namespace audio_engine {

// NTP epoch: January 1, 1900
// Unix epoch: January 1, 1970
// Offset: 70 years = 2,208,988,800 seconds
constexpr uint64_t NTP_UNIX_EPOCH_OFFSET = 2208988800ULL;

ReferenceClockManager::ReferenceClockManager() 
    : m_reference_epoch(std::chrono::steady_clock::now()) {
    // Initialize NTP offset based on system time
    auto system_now = std::chrono::system_clock::now();
    auto steady_now = std::chrono::steady_clock::now();
    
    // Calculate offset between system clock and steady clock
    auto system_duration = system_now.time_since_epoch();
    auto system_seconds = std::chrono::duration_cast<std::chrono::seconds>(system_duration).count();
    
    // Convert to NTP time
    uint64_t ntp_seconds = system_seconds + NTP_UNIX_EPOCH_OFFSET;
    
    // Store initial offset
    m_ntp_offset_ns.store(0, std::memory_order_relaxed);
}

ReferenceClockManager& ReferenceClockManager::get_instance() {
    static ReferenceClockManager instance;
    return instance;
}

std::chrono::steady_clock::time_point ReferenceClockManager::now() const {
    return std::chrono::steady_clock::now();
}

uint64_t ReferenceClockManager::reference_time_to_ntp(
    std::chrono::steady_clock::time_point ref_time) const {
    
    int64_t offset_ns = m_ntp_offset_ns.load(std::memory_order_relaxed);
    return steady_clock_to_ntp(ref_time, offset_ns);
}

std::chrono::steady_clock::time_point ReferenceClockManager::ntp_to_reference_time(
    uint64_t ntp_timestamp) const {
    
    int64_t offset_ns = m_ntp_offset_ns.load(std::memory_order_relaxed);
    return ntp_to_steady_clock(ntp_timestamp, offset_ns);
}

void ReferenceClockManager::register_ntp_source(
    const std::string& source_id,
    uint64_t ntp_timestamp,
    std::chrono::steady_clock::time_point received_at) {
    
    std::lock_guard<std::mutex> lock(m_sync_mutex);
    
    // Calculate what the NTP time should be based on steady clock
    auto current_ntp = reference_time_to_ntp(received_at);
    
    // Calculate offset
    int64_t offset = static_cast<int64_t>(ntp_timestamp) - static_cast<int64_t>(current_ntp);
    
    // Update offset (simple assignment for now, could use filtering)
    m_ntp_offset_ns.store(offset, std::memory_order_relaxed);
}

std::chrono::nanoseconds ReferenceClockManager::get_ntp_offset() const {
    return std::chrono::nanoseconds(m_ntp_offset_ns.load(std::memory_order_relaxed));
}

void ReferenceClockManager::reset() {
    m_reference_epoch = std::chrono::steady_clock::now();
    m_ntp_offset_ns.store(0, std::memory_order_relaxed);
}

// Helper: Convert steady_clock to NTP timestamp
uint64_t ReferenceClockManager::steady_clock_to_ntp(
    std::chrono::steady_clock::time_point tp, int64_t offset_ns) {
    
    // Get system time at this moment
    auto system_now = std::chrono::system_clock::now();
    auto system_duration = system_now.time_since_epoch();
    auto unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(system_duration).count();
    auto unix_fraction = std::chrono::duration_cast<std::chrono::nanoseconds>(
        system_duration - std::chrono::seconds(unix_seconds)).count();
    
    // Convert to NTP
    uint64_t ntp_seconds = unix_seconds + NTP_UNIX_EPOCH_OFFSET;
    
    // NTP fraction: (nanoseconds * 2^32) / 1e9
    uint64_t ntp_fraction = (static_cast<uint64_t>(unix_fraction) << 32) / 1000000000ULL;
    
    // Combine into 64-bit NTP timestamp
    uint64_t ntp_timestamp = (ntp_seconds << 32) | ntp_fraction;
    
    // Apply offset
    ntp_timestamp += offset_ns / 1000000000ULL;
    
    return ntp_timestamp;
}

std::chrono::steady_clock::time_point ReferenceClockManager::ntp_to_steady_clock(
    uint64_t ntp_ts, int64_t offset_ns) {
    
    // Extract seconds and fraction
    uint64_t ntp_seconds = ntp_ts >> 32;
    uint64_t ntp_fraction = ntp_ts & 0xFFFFFFFF;
    
    // Convert to Unix time
    int64_t unix_seconds = ntp_seconds - NTP_UNIX_EPOCH_OFFSET;
    int64_t unix_nanos = (ntp_fraction * 1000000000ULL) >> 32;
    
    // Apply offset
    unix_seconds -= offset_ns / 1000000000ULL;
    unix_nanos -= offset_ns % 1000000000ULL;
    
    // Create system_clock time_point
    auto unix_duration = std::chrono::seconds(unix_seconds) + std::chrono::nanoseconds(unix_nanos);
    auto system_tp = std::chrono::system_clock::time_point(unix_duration);
    
    // Convert to steady_clock (approximate - assumes clocks are synchronized)
    auto system_now = std::chrono::system_clock::now();
    auto steady_now = std::chrono::steady_clock::now();
    auto diff = system_tp - system_now;
    
    return steady_now + diff;
}

} // namespace audio_engine
} // namespace screamrouter