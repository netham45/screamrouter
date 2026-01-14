#ifndef SCREAMROUTER_AUDIO_FORMAT_PROBE_H
#define SCREAMROUTER_AUDIO_FORMAT_PROBE_H

#include "sap_listener/sap_types.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace screamrouter {
namespace audio {

/**
 * @brief Probes raw PCM audio data to detect format parameters.
 *
 * When SAP metadata is unavailable, this class accumulates raw audio
 * payload data and uses statistical analysis to determine:
 * - Sample rate (from byte rate over time)
 * - Channels and bit depth (from discontinuity scoring)
 * - Endianness (from byte volatility analysis)
 */
class AudioFormatProbe {
public:
    /// Minimum bytes required before attempting detection
    static constexpr size_t kMinProbeBytes = 48000 * 2 * 2;  // ~0.5s at 48kHz stereo 16-bit

    /// Target bytes for high-confidence detection
    static constexpr size_t kTargetProbeBytes = 48000 * 2 * 2 * 3;  // ~1.5s

    /// Maximum bytes to buffer during probing
    static constexpr size_t kMaxProbeBytes = 48000 * 2 * 4 * 2;  // ~2s at 48kHz stereo 32-bit

    AudioFormatProbe();

    /**
     * @brief Add raw payload data from an RTP packet.
     * @param payload Raw audio bytes from RTP payload.
     * @param received_time Wall-clock time of packet reception.
     */
    void add_data(const std::vector<uint8_t>& payload,
                  std::chrono::steady_clock::time_point received_time);

    /**
     * @brief Check if sufficient data has been collected for detection.
     * @return true if detection can proceed with reasonable confidence.
     */
    bool has_sufficient_data() const;

    /**
     * @brief Check if detection has been finalized.
     * @return true if format has been determined.
     */
    bool is_detection_complete() const;

    /**
     * @brief Run detection algorithms and finalize format.
     * @return true if detection succeeded, false if insufficient data or ambiguous.
     */
    bool finalize_detection();

    /**
     * @brief Get the detected format parameters.
     * @return StreamProperties with detected format (only valid after finalize_detection).
     */
    const StreamProperties& get_detected_format() const;

    /**
     * @brief Get detection confidence score (0.0 - 1.0).
     * @return Confidence level of the detection.
     */
    float get_confidence() const;

    /**
     * @brief Reset probe state for reuse.
     */
    void reset();

    /**
     * @brief Set the probe duration in milliseconds.
     * @param duration_ms Minimum time to probe before detection (default 500ms).
     */
    void set_probe_duration_ms(double duration_ms);

    /**
     * @brief Set the minimum bytes required before detection.
     * @param min_bytes Minimum bytes before detection (default 5000).
     */
    void set_probe_min_bytes(size_t min_bytes);

private:
    struct InterchannelStats {
        double normalized_difference = 1.0;  ///< Avg |ch - ref| normalized by max amp
        double relative_difference = 1.0;    ///< Cross diff relative to sequential diff
    };

    /// Candidate format for brute-force testing
    struct FormatCandidate {
        int channels;
        int bit_depth;
        Endianness endianness;
        double score;  // Lower is better (fewer discontinuities)
        InterchannelStats interchannel_stats;
    };

    /**
     * @brief Compute discontinuity score for a given format interpretation.
     *
     * Interprets the buffered bytes as samples with the given format,
     * then counts large amplitude jumps between consecutive samples.
     */
    double compute_discontinuity_score(int channels, int bit_depth, Endianness endianness) const;

    /**
     * @brief Measure cross-channel similarity statistics.
     *
     * Captures both the absolute difference between channels and how that
     * compares to the average difference between consecutive samples in the
     * stream. This helps distinguish "true" mono streams from stereo streams
     * that merely duplicate a channel.
     */
    InterchannelStats compute_interchannel_stats(int channels, int bit_depth, Endianness endianness) const;

    /**
     * @brief Detect endianness using byte volatility analysis.
     *
     * For each sample, compares change frequency of high vs low bytes.
     * The byte that changes more frequently is the LSB.
     */
    Endianness detect_endianness(int bit_depth) const;

    /**
     * @brief Estimate sample rate from accumulated byte rate.
     * @param channels Detected channel count.
     * @param bit_depth Detected bit depth.
     * @return Estimated sample rate, rounded to nearest common value.
     */
    int estimate_sample_rate(int channels, int bit_depth) const;

    /**
     * @brief Round to nearest common sample rate.
     */
    static int round_to_common_sample_rate(int raw_rate);

    /**
     * @brief Detect codec type by trying multiple decoders.
     * @return Detected StreamCodec (PCMU, PCMA, OPUS, MP3) or UNKNOWN.
     */
    StreamCodec detect_codec() const;

    /**
     * @brief Compute discontinuity score for μ-law decoded audio.
     * @return Discontinuity score (lower = smoother = more likely correct).
     */
    double compute_ulaw_discontinuity() const;

    /**
     * @brief Compute discontinuity score for A-law decoded audio.
     * @return Discontinuity score (lower = smoother = more likely correct).
     */
    double compute_alaw_discontinuity() const;

    /**
     * @brief Compute discontinuity score for Opus decoded audio.
     * @return Discontinuity score, or MAX if decode fails.
     */
    double compute_opus_discontinuity() const;

    /**
     * @brief Compute discontinuity score for MP3 decoded audio.
     * @return Discontinuity score, or MAX if decode fails.
     */
    double compute_mp3_discontinuity() const;

    /// Accumulated raw audio data
    std::vector<uint8_t> probe_buffer_;

    /// Time of first packet received
    std::chrono::steady_clock::time_point first_packet_time_;

    /// Time of most recent packet received
    std::chrono::steady_clock::time_point last_packet_time_;

    /// Total bytes received (may exceed buffer if overflow occurred)
    size_t total_bytes_received_ = 0;

    /// Detection result
    StreamProperties detected_format_;

    /// Confidence score (0.0 - 1.0)
    float confidence_ = 0.0f;

    /// Whether detection has been finalized
    bool detection_complete_ = false;

    /// Configurable probe duration in milliseconds
    double probe_duration_ms_ = 500.0;

    /// Configurable minimum bytes for detection
    size_t probe_min_bytes_ = 5000;
};

}  // namespace audio
}  // namespace screamrouter

#endif  // SCREAMROUTER_AUDIO_FORMAT_PROBE_H
