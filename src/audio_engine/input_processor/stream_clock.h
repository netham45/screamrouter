#ifndef STREAM_CLOCK_H
#define STREAM_CLOCK_H

#include <chrono>
#include <cstdint>

namespace screamrouter {
namespace audio {

/**
 * @class StreamClock
 * @brief Models the relationship between a remote RTP clock and the local steady_clock.
 *
 * This class uses a Kalman filter to estimate the true offset and drift rate of a remote
 * RTP stream's clock relative to the local monotonic clock. This provides a stable
 * foundation for jitter calculation and playout scheduling, immune to network jitter.
 */
class StreamClock {
public:
    StreamClock(double sample_rate);

    /**
     * @brief Updates the clock model with a new packet's timing information.
     * @param rtp_timestamp The RTP timestamp from the packet header.
     * @param arrival_time The time the packet was received, based on a local steady_clock.
     */
    void update(uint32_t rtp_timestamp, std::chrono::steady_clock::time_point arrival_time);

    /**
     * @brief Calculates the expected arrival time for a given RTP timestamp based on the current model.
     * @param rtp_timestamp The RTP timestamp to project.
     * @return The estimated time point on the local steady_clock.
     */
    std::chrono::steady_clock::time_point get_expected_arrival_time(uint32_t rtp_timestamp) const;

    /**
     * @brief Resets the filter to an initial state.
     */
    void reset();

    /**
     * @brief Whether the clock has received enough data to produce estimates.
     */
    bool is_initialized() const;

    /**
     * @brief Returns the most recent estimate of the RTP to steady clock offset in seconds.
     */
    double get_offset_seconds() const;

    /**
     * @brief Returns the estimated relative drift expressed in parts-per-million.
     */
    double get_drift_ppm() const;

    /**
     * @brief Returns the last innovation (prediction error) observed during update in seconds.
     */
    double get_last_innovation_seconds() const;

    /**
     * @brief Returns the last measured offset (arrival - RTP) in seconds.
     */
    double get_last_measured_offset_seconds() const;

    /**
     * @brief Returns the time the clock state was most recently updated.
     */
    std::chrono::steady_clock::time_point get_last_update_time() const;

private:
    // The sample rate of the RTP clock.
    const double m_sample_rate;

    // RTP unwrapping/reference
    bool m_has_reference;
    uint32_t m_last_rtp_timestamp;
    uint64_t m_unwrapped_rtp;
    std::chrono::steady_clock::time_point m_reference_arrival_time;

    // Kalman filter state variables
    // x = [offset, drift]'
    double m_offset; // Estimated offset of RTP clock from local clock (in seconds)
    double m_drift;  // Estimated drift rate of RTP clock relative to local clock

    // Kalman filter covariance matrix P
    // P = [[p00, p01], [p10, p11]]
    double m_p[2][2];

    // Kalman filter process and measurement noise
    double m_process_noise;
    double m_measurement_noise;

    // Timestamp of the last update to calculate delta_t
    std::chrono::steady_clock::time_point m_last_update_time;
    bool m_is_initialized;

    double m_last_innovation;
    double m_last_measured_offset;
};

} // namespace audio
} // namespace screamrouter

#endif // STREAM_CLOCK_H
