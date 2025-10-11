#include "timestamp_mapper.h"
#include <algorithm>

namespace screamrouter {
namespace audio_engine {

TimestampMapper::TimestampMapper(const std::string& instance_id)
    : m_instance_id(instance_id) {}

void TimestampMapper::add_mapping(const TimestampMapping& mapping) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Add to deque
    m_mappings.push_back(mapping);
    
    // Update statistics
    update_latency_stats(mapping.processing_latency_ms);
    
    // Limit size
    if (m_mappings.size() > MAX_MAPPINGS) {
        m_mappings.pop_front();
    }
}

void TimestampMapper::update_latency_stats(double latency_ms) {
    if (m_latency_sample_count == 0) {
        m_avg_processing_latency_ms = latency_ms;
        m_min_processing_latency_ms = latency_ms;
        m_max_processing_latency_ms = latency_ms;
    } else {
        // Running average
        m_avg_processing_latency_ms = 
            (m_avg_processing_latency_ms * m_latency_sample_count + latency_ms) / 
            (m_latency_sample_count + 1);
        
        m_min_processing_latency_ms = std::min(m_min_processing_latency_ms, latency_ms);
        m_max_processing_latency_ms = std::max(m_max_processing_latency_ms, latency_ms);
    }
    
    m_latency_sample_count++;
}

double TimestampMapper::get_average_processing_latency_ms() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_avg_processing_latency_ms;
}

double TimestampMapper::get_min_processing_latency_ms() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_min_processing_latency_ms;
}

double TimestampMapper::get_max_processing_latency_ms() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_max_processing_latency_ms;
}

size_t TimestampMapper::get_mapping_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mappings.size();
}

std::optional<uint32_t> TimestampMapper::get_output_timestamp_for_input(
    uint32_t input_rtp_ts) const {
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto mapping = find_mapping_for_input(input_rtp_ts);
    if (mapping.has_value()) {
        return mapping->output_rtp_timestamp;
    }
    
    return std::nullopt;
}

std::optional<TimestampMapping> TimestampMapper::find_mapping_for_input(
    uint32_t input_ts) const {
    
    // Search backward (most recent first)
    for (auto it = m_mappings.rbegin(); it != m_mappings.rend(); ++it) {
        if (it->input_rtp_timestamp == input_ts) {
            return *it;
        }
        
        // Check for timestamp wraparound (RTP timestamps are 32-bit and wrap)
        int64_t diff = static_cast<int64_t>(input_ts) - static_cast<int64_t>(it->input_rtp_timestamp);
        if (std::abs(diff) < 100) { // Close enough (within ~2ms at 48kHz)
            return *it;
        }
    }
    
    return std::nullopt;
}

std::chrono::steady_clock::time_point TimestampMapper::calculate_output_playout_time(
    uint32_t input_rtp_ts,
    std::chrono::steady_clock::time_point input_arrival_time,
    double jitter_buffer_delay_ms) const {
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Calculate delays in nanoseconds for proper type conversion
    auto jitter_delay = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double, std::milli>(jitter_buffer_delay_ms));
    auto processing_delay = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double, std::milli>(m_avg_processing_latency_ms));
    
    // Base playout time: arrival + jitter buffer + processing latency
    return input_arrival_time + jitter_delay + processing_delay;
}

void TimestampMapper::cleanup_old_mappings(std::chrono::seconds max_age) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto now = std::chrono::steady_clock::now();
    auto cutoff_time = now - max_age;
    
    // Remove mappings older than cutoff
    auto it = m_mappings.begin();
    while (it != m_mappings.end()) {
        if (it->input_arrival_time < cutoff_time) {
            it = m_mappings.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace audio_engine
} // namespace screamrouter