#include "stream_clock.h"
#include <cmath>

namespace screamrouter {
namespace audio {

// Constants for the Kalman Filter
// These values are chosen empirically and may require tuning.
constexpr double INITIAL_UNCERTAINTY = 1.0;
constexpr double PROCESS_NOISE_Q = 1e-5; // How much we trust our prediction model (lower = more trust)
constexpr double MEASUREMENT_NOISE_R = 1e-2; // How much we trust the measurement (lower = more trust)


StreamClock::StreamClock(double sample_rate)
    : m_sample_rate(sample_rate),
      m_process_noise(PROCESS_NOISE_Q),
      m_measurement_noise(MEASUREMENT_NOISE_R) {
    reset();
}

void StreamClock::reset() {
    m_is_initialized = false;
    m_offset = 0.0;
    m_drift = 0.0;
    m_has_reference = false;
    m_last_rtp_timestamp = 0;
    m_unwrapped_rtp = 0;
    m_reference_arrival_time = {};
    m_p[0][0] = INITIAL_UNCERTAINTY;
    m_p[0][1] = 0.0;
    m_p[1][0] = 0.0;
    m_p[1][1] = INITIAL_UNCERTAINTY;
    m_last_innovation = 0.0;
    m_last_measured_offset = 0.0;
    m_last_update_time = {};
}

void StreamClock::update(uint32_t rtp_timestamp, std::chrono::steady_clock::time_point arrival_time) {
    if (!m_has_reference) {
        m_reference_arrival_time = arrival_time;
        m_last_rtp_timestamp = rtp_timestamp;
        m_unwrapped_rtp = 0;
        m_offset = 0.0;
        m_drift = 0.0;
        m_last_update_time = arrival_time;
        m_is_initialized = true;
        m_has_reference = true;
        m_last_measured_offset = 0.0;
        m_last_innovation = 0.0;
        return;
    }

    const uint32_t rtp_delta = static_cast<uint32_t>(rtp_timestamp - m_last_rtp_timestamp);
    m_unwrapped_rtp += static_cast<uint64_t>(rtp_delta);
    m_last_rtp_timestamp = rtp_timestamp;

    const double rtp_time_sec = static_cast<double>(m_unwrapped_rtp) / m_sample_rate;
    const double arrival_time_sec =
        std::chrono::duration<double>(arrival_time - m_reference_arrival_time).count();

    double delta_t = std::chrono::duration<double>(arrival_time - m_last_update_time).count();
    m_last_update_time = arrival_time;

    // --- Prediction Step ---
    // Predict state: x_pred = F * x
    // No change in drift, so drift_pred = drift
    // offset_pred = offset + drift * delta_t
    m_offset += m_drift * delta_t;

    // Predict covariance: P_pred = F * P * F' + Q
    m_p[0][0] += delta_t * (2 * m_p[1][0] + delta_t * m_p[1][1]) + m_process_noise;
    m_p[0][1] += delta_t * m_p[1][1];
    m_p[1][0] += delta_t * m_p[1][1];
    m_p[1][1] += m_process_noise;

    // --- Update Step ---
    // Measurement residual (innovation): y = z - H * x_pred
    // z is the measured offset
    double measured_offset = arrival_time_sec - rtp_time_sec;
    double innovation = measured_offset - m_offset;

    // Innovation covariance: S = H * P_pred * H' + R
    // H = [1, 0]
    double innovation_covariance = m_p[0][0] + m_measurement_noise;

    // Kalman gain: K = P_pred * H' * S^-1
    double K[2];
    K[0] = m_p[0][0] / innovation_covariance;
    K[1] = m_p[1][0] / innovation_covariance;

    // Update state: x = x_pred + K * y
    m_offset += K[0] * innovation;
    m_drift += K[1] * innovation;

    // Update covariance: P = (I - K * H) * P_pred
    double p00_temp = m_p[0][0];
    double p01_temp = m_p[0][1];

    m_p[0][0] -= K[0] * p00_temp;
    m_p[0][1] -= K[0] * p01_temp;
    m_p[1][0] -= K[1] * p00_temp;
    m_p[1][1] -= K[1] * p01_temp;

    m_last_innovation = innovation;
    m_last_measured_offset = measured_offset;
}

std::chrono::steady_clock::time_point StreamClock::get_expected_arrival_time(uint32_t rtp_timestamp) const {
    if (!m_is_initialized) {
        return std::chrono::steady_clock::time_point::min();
    }
    uint64_t target_unwrapped = m_unwrapped_rtp;
    const uint32_t delta = static_cast<uint32_t>(rtp_timestamp - m_last_rtp_timestamp);
    target_unwrapped += static_cast<uint64_t>(delta);

    const double rtp_time_sec = static_cast<double>(target_unwrapped) / m_sample_rate;
    const double expected_arrival_sec = rtp_time_sec + m_offset;
    const auto expected_since_ref = std::chrono::duration<double>(expected_arrival_sec);
    return m_reference_arrival_time + std::chrono::duration_cast<std::chrono::steady_clock::duration>(expected_since_ref);
}

bool StreamClock::is_initialized() const {
    return m_is_initialized;
}

double StreamClock::get_offset_seconds() const {
    return m_offset;
}

double StreamClock::get_drift_ppm() const {
    return m_drift * 1'000'000.0;
}

double StreamClock::get_last_innovation_seconds() const {
    return m_last_innovation;
}

double StreamClock::get_last_measured_offset_seconds() const {
    return m_last_measured_offset;
}

std::chrono::steady_clock::time_point StreamClock::get_last_update_time() const {
    return m_last_update_time;
}

} // namespace audio
} // namespace screamrouter
