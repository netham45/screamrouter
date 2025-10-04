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

private:
    // The sample rate of the RTP clock.
    const double m_sample_rate;

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
};

} // namespace audio
} // namespace screamrouter

#endif // STREAM_CLOCK_H