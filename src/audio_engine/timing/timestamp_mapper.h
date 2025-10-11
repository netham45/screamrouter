#pragma once

#include <chrono>
#include <string>
#include <deque>
#include <mutex>
#include <optional>
#include <cstdint>

namespace screamrouter {
namespace audio_engine {

/**
 * Represents a mapping between input and output timestamps
 */
struct TimestampMapping {
    uint32_t input_rtp_timestamp;
    uint32_t output_rtp_timestamp;
    std::chrono::steady_clock::time_point input_arrival_time;
    std::chrono::steady_clock::time_point expected_output_time;
    double processing_latency_ms;
    double resampling_ratio;
    
    TimestampMapping() = default;
    TimestampMapping(uint32_t input_ts, uint32_t output_ts,
                    std::chrono::steady_clock::time_point input_time,
                    std::chrono::steady_clock::time_point output_time,
                    double latency_ms, double ratio)
        : input_rtp_timestamp(input_ts)
        , output_rtp_timestamp(output_ts)
        , input_arrival_time(input_time)
        , expected_output_time(output_time)
        , processing_latency_ms(latency_ms)
        , resampling_ratio(ratio) {}
};

/**
 * Tracks timestamp mappings for a single processing path
 */
class TimestampMapper {
public:
    explicit TimestampMapper(const std::string& instance_id);
    
    // Add a new mapping
    void add_mapping(const TimestampMapping& mapping);
    
    // Query output timestamp for a given input timestamp
    std::optional<uint32_t> get_output_timestamp_for_input(uint32_t input_rtp_ts) const;
    
    // Calculate expected output playout time for an input packet
    std::chrono::steady_clock::time_point calculate_output_playout_time(
        uint32_t input_rtp_ts,
        std::chrono::steady_clock::time_point input_arrival_time,
        double jitter_buffer_delay_ms) const;
    
    // Get measured processing latency statistics
    double get_average_processing_latency_ms() const;
    double get_min_processing_latency_ms() const;
    double get_max_processing_latency_ms() const;
    
    // Cleanup old mappings
    void cleanup_old_mappings(std::chrono::seconds max_age);
    
    // Get instance identifier
    const std::string& get_instance_id() const { return m_instance_id; }
    
    // Get statistics
    size_t get_mapping_count() const;

private:
    std::string m_instance_id;
    std::deque<TimestampMapping> m_mappings;
    mutable std::mutex m_mutex;
    
    // Statistics
    double m_avg_processing_latency_ms = 0.0;
    double m_min_processing_latency_ms = 0.0;
    double m_max_processing_latency_ms = 0.0;
    size_t m_latency_sample_count = 0;
    
    // Configuration
    static constexpr size_t MAX_MAPPINGS = 1000;
    int m_sample_rate = 48000;
    
    // Helper methods
    void update_latency_stats(double latency_ms);
    std::optional<TimestampMapping> find_mapping_for_input(uint32_t input_ts) const;
};

} // namespace audio_engine
} // namespace screamrouter