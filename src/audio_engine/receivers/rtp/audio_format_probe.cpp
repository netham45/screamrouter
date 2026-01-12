#include "audio_format_probe.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include <opus/opus.h>
#include <lame/lame.h>

namespace screamrouter {
namespace audio {

namespace {

/// Common sample rates to round to
constexpr int kCommonSampleRates[] = {
    8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000
};

/// Discontinuity threshold as fraction of max amplitude
constexpr double kDiscontinuityThreshold = 0.3;

/// Minimum variance required to avoid "silence" detection
constexpr double kMinVarianceThreshold = 0.001;

/// Read a sample from buffer with given format
int64_t read_sample(const uint8_t* data, int bit_depth, Endianness endianness) {
    switch (bit_depth) {
        case 8:
            // 8-bit is unsigned, center at 128
            return static_cast<int64_t>(data[0]) - 128;
        case 16: {
            int16_t val;
            if (endianness == Endianness::LITTLE) {
                val = static_cast<int16_t>(data[0] | (data[1] << 8));
            } else {
                val = static_cast<int16_t>((data[0] << 8) | data[1]);
            }
            return val;
        }
        case 24: {
            int32_t val;
            if (endianness == Endianness::LITTLE) {
                val = data[0] | (data[1] << 8) | (data[2] << 16);
            } else {
                val = (data[0] << 16) | (data[1] << 8) | data[2];
            }
            // Sign extend from 24 to 32 bits
            if (val & 0x800000) {
                val |= 0xFF000000;
            }
            return val;
        }
        case 32: {
            int32_t val;
            if (endianness == Endianness::LITTLE) {
                val = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
            } else {
                val = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            }
            return val;
        }
        default:
            return 0;
    }
}

/// Get max amplitude for a bit depth
int64_t max_amplitude(int bit_depth) {
    switch (bit_depth) {
        case 8:  return 127;
        case 16: return 32767;
        case 24: return 8388607;
        case 32: return 2147483647LL;
        default: return 32767;
    }
}

}  // namespace

AudioFormatProbe::AudioFormatProbe() {
    probe_buffer_.reserve(kTargetProbeBytes);
}

void AudioFormatProbe::add_data(const std::vector<uint8_t>& payload,
                                 std::chrono::steady_clock::time_point received_time) {
    if (detection_complete_) {
        return;
    }

    if (total_bytes_received_ == 0) {
        first_packet_time_ = received_time;
    }
    last_packet_time_ = received_time;
    total_bytes_received_ += payload.size();

    // Add to buffer, respecting max size
    size_t space_remaining = kMaxProbeBytes - probe_buffer_.size();
    size_t bytes_to_add = std::min(payload.size(), space_remaining);
    if (bytes_to_add > 0) {
        probe_buffer_.insert(probe_buffer_.end(),
                            payload.begin(),
                            payload.begin() + bytes_to_add);
    }
}

bool AudioFormatProbe::has_sufficient_data() const {
    if (detection_complete_) {
        return true;
    }

    // Need minimum bytes and minimum time window
    if (probe_buffer_.size() < kMinProbeBytes) {
        return false;
    }

    auto duration = last_packet_time_ - first_packet_time_;
    return duration >= std::chrono::milliseconds(500);
}

bool AudioFormatProbe::is_detection_complete() const {
    return detection_complete_;
}

bool AudioFormatProbe::finalize_detection() {
    if (detection_complete_) {
        return true;
    }

    if (!has_sufficient_data()) {
        return false;
    }

    // First, try to detect encoded codecs (PCMU, PCMA, Opus)
    StreamCodec detected_codec = detect_codec();
    if (detected_codec != StreamCodec::UNKNOWN) {
        detected_format_.codec = detected_codec;
        
        // Set format parameters based on codec
        if (detected_codec == StreamCodec::PCMU || detected_codec == StreamCodec::PCMA) {
            // Companded audio: always 8-bit, but channels can vary
            // Estimate byte rate and find best channel/sample-rate match
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                last_packet_time_ - first_packet_time_);
            double seconds = duration.count() / 1000.0;
            double byte_rate = (seconds > 0) ? total_bytes_received_ / seconds : 0;
            
            // Try common channel counts and find closest sample rate match
            const int channel_options[] = {1, 2, 6, 8};
            int best_channels = 1;
            int best_sample_rate = 8000;
            int min_error = std::numeric_limits<int>::max();
            
            for (int ch : channel_options) {
                int raw_rate = static_cast<int>(byte_rate / ch);
                int rounded_rate = round_to_common_sample_rate(raw_rate);
                int error = std::abs(raw_rate - rounded_rate);
                if (error < min_error) {
                    min_error = error;
                    best_channels = ch;
                    best_sample_rate = rounded_rate;
                }
            }
            
            detected_format_.bit_depth = 8;
            detected_format_.channels = best_channels;
            detected_format_.sample_rate = best_sample_rate;
            detected_format_.endianness = Endianness::BIG;
        } else if (detected_codec == StreamCodec::OPUS) {
            // Opus: always 48kHz, channels detected via decode-and-score
            // Standard Opus decoder only supports 1-2 channels
            const int channel_options[] = {1, 2};
            int best_channels = 2;
            double min_discontinuity = std::numeric_limits<double>::max();
            
            for (int ch : channel_options) {
                int error = 0;
                OpusDecoder* decoder = opus_decoder_create(48000, ch, &error);
                if (error != OPUS_OK || !decoder) continue;
                
                constexpr int kMaxFrameSamples = 5760;
                std::vector<int16_t> pcm(kMaxFrameSamples * ch);
                
                // Decode first packet only (probe buffer is concatenated payloads)
                size_t packet_len = std::min(probe_buffer_.size(), size_t(1500));
                
                int decoded_samples = opus_decode(decoder,
                    probe_buffer_.data(),
                    static_cast<opus_int32>(packet_len),
                    pcm.data(), kMaxFrameSamples, 0);
                
                opus_decoder_destroy(decoder);
                
                if (decoded_samples <= 0) continue;
                
                // Compute discontinuity score
                double discontinuity = 0.0;
                int total_samples = decoded_samples * ch;
                for (int i = 1; i < total_samples; ++i) {
                    int32_t diff = std::abs(static_cast<int32_t>(pcm[i]) - pcm[i-1]);
                    if (diff > 6500) {
                        discontinuity += static_cast<double>(diff) / 32767.0;
                    }
                }
                discontinuity /= total_samples;
                
                if (discontinuity < min_discontinuity) {
                    min_discontinuity = discontinuity;
                    best_channels = ch;
                }
            }
            
            detected_format_.bit_depth = 16;
            detected_format_.channels = best_channels;
            detected_format_.sample_rate = 48000;
            detected_format_.endianness = Endianness::LITTLE;
        }
        
        confidence_ = 0.75f;
        detection_complete_ = true;
        return true;
    }

    // Fall through to PCM brute-force detection

    // Brute-force all format combinations
    std::vector<FormatCandidate> candidates;

    // Test common channel/bit-depth combinations
    const int channel_options[] = {1, 2, 6, 8};
    const int bit_depth_options[] = {8, 16, 24, 32};

    for (int channels : channel_options) {
        for (int bit_depth : bit_depth_options) {
            int bytes_per_frame = channels * (bit_depth / 8);
            if (probe_buffer_.size() < static_cast<size_t>(bytes_per_frame) * 100) {
                continue;  // Not enough frames to analyze
            }

            // Test both endianness options for bit depths > 8
            if (bit_depth == 8) {
                double score = compute_discontinuity_score(channels, bit_depth, Endianness::LITTLE);
                candidates.push_back({channels, bit_depth, Endianness::LITTLE, score});
            } else {
                // Use byte volatility to pick endianness, then score
                Endianness detected_endian = detect_endianness(bit_depth);
                double score = compute_discontinuity_score(channels, bit_depth, detected_endian);
                candidates.push_back({channels, bit_depth, detected_endian, score});
            }
        }
    }

    if (candidates.empty()) {
        return false;
    }

    // Sort by score (lower is better)
    std::sort(candidates.begin(), candidates.end(),
              [](const FormatCandidate& a, const FormatCandidate& b) {
                  return a.score < b.score;
              });

    const FormatCandidate& best = candidates[0];

    // Calculate confidence based on score separation
    if (candidates.size() > 1) {
        double second_best_score = candidates[1].score;
        if (best.score > 0 && second_best_score > 0) {
            double ratio = second_best_score / best.score;
            confidence_ = std::min(1.0f, static_cast<float>((ratio - 1.0) / 2.0));
        } else if (best.score == 0) {
            confidence_ = 0.5f;  // Perfect score but might be silence
        } else {
            confidence_ = 0.8f;
        }
    } else {
        confidence_ = 0.6f;  // Only one candidate
    }

    // Estimate sample rate
    int sample_rate = estimate_sample_rate(best.channels, best.bit_depth);

    // Populate detected format
    detected_format_.channels = best.channels;
    detected_format_.bit_depth = best.bit_depth;
    detected_format_.endianness = best.endianness;
    detected_format_.sample_rate = sample_rate;
    detected_format_.codec = StreamCodec::PCM;

    detection_complete_ = true;
    return true;
}

const StreamProperties& AudioFormatProbe::get_detected_format() const {
    return detected_format_;
}

float AudioFormatProbe::get_confidence() const {
    return confidence_;
}

void AudioFormatProbe::reset() {
    probe_buffer_.clear();
    probe_buffer_.reserve(kTargetProbeBytes);
    first_packet_time_ = {};
    last_packet_time_ = {};
    total_bytes_received_ = 0;
    detected_format_ = {};
    confidence_ = 0.0f;
    detection_complete_ = false;
}

double AudioFormatProbe::compute_discontinuity_score(int channels, int bit_depth, Endianness endianness) const {
    const int bytes_per_sample = bit_depth / 8;
    const int bytes_per_frame = channels * bytes_per_sample;

    if (probe_buffer_.size() < static_cast<size_t>(bytes_per_frame) * 10) {
        return std::numeric_limits<double>::max();
    }

    const size_t num_frames = probe_buffer_.size() / bytes_per_frame;
    const int64_t max_amp = max_amplitude(bit_depth);
    const int64_t threshold = static_cast<int64_t>(max_amp * kDiscontinuityThreshold);

    double total_discontinuity = 0.0;
    double total_variance = 0.0;
    int64_t sum = 0;
    size_t sample_count = 0;

    // First pass: compute mean
    for (size_t frame = 0; frame < num_frames; ++frame) {
        for (int ch = 0; ch < channels; ++ch) {
            const uint8_t* sample_ptr = probe_buffer_.data() + frame * bytes_per_frame + ch * bytes_per_sample;
            int64_t sample = read_sample(sample_ptr, bit_depth, endianness);
            sum += sample;
            sample_count++;
        }
    }
    double mean = static_cast<double>(sum) / sample_count;

    // Second pass: compute variance and discontinuities
    int64_t prev_samples[8] = {0};  // Max 8 channels
    bool first_frame = true;

    for (size_t frame = 0; frame < num_frames; ++frame) {
        for (int ch = 0; ch < channels; ++ch) {
            const uint8_t* sample_ptr = probe_buffer_.data() + frame * bytes_per_frame + ch * bytes_per_sample;
            int64_t sample = read_sample(sample_ptr, bit_depth, endianness);

            double diff_from_mean = static_cast<double>(sample) - mean;
            total_variance += diff_from_mean * diff_from_mean;

            if (!first_frame) {
                int64_t discontinuity = std::abs(sample - prev_samples[ch]);
                if (discontinuity > threshold) {
                    total_discontinuity += static_cast<double>(discontinuity) / max_amp;
                }
            }
            prev_samples[ch] = sample;
        }
        first_frame = false;
    }

    // Normalize variance
    double variance = total_variance / sample_count;
    double normalized_variance = variance / (static_cast<double>(max_amp) * max_amp);

    // If variance is too low (silence), return high score to deprioritize
    if (normalized_variance < kMinVarianceThreshold) {
        return std::numeric_limits<double>::max() / 2.0;
    }

    // Return discontinuity count normalized by number of frames
    return total_discontinuity / num_frames;
}

Endianness AudioFormatProbe::detect_endianness(int bit_depth) const {
    if (bit_depth == 8) {
        return Endianness::LITTLE;  // Doesn't matter for 8-bit
    }

    const int bytes_per_sample = bit_depth / 8;
    const size_t num_samples = probe_buffer_.size() / bytes_per_sample;

    if (num_samples < 100) {
        return Endianness::BIG;  // Default fallback
    }

    // Count how often each byte position changes between consecutive samples
    std::vector<size_t> byte_changes(bytes_per_sample, 0);

    for (size_t i = 1; i < num_samples; ++i) {
        const uint8_t* curr = probe_buffer_.data() + i * bytes_per_sample;
        const uint8_t* prev = probe_buffer_.data() + (i - 1) * bytes_per_sample;

        for (int b = 0; b < bytes_per_sample; ++b) {
            if (curr[b] != prev[b]) {
                byte_changes[b]++;
            }
        }
    }

    // The byte that changes most frequently is likely the LSB
    size_t max_changes = 0;
    int most_volatile_byte = 0;
    for (int b = 0; b < bytes_per_sample; ++b) {
        if (byte_changes[b] > max_changes) {
            max_changes = byte_changes[b];
            most_volatile_byte = b;
        }
    }

    // If byte 0 is most volatile, it's little-endian (LSB first)
    // If last byte is most volatile, it's big-endian (MSB first, LSB last)
    if (most_volatile_byte == 0) {
        return Endianness::LITTLE;
    } else if (most_volatile_byte == bytes_per_sample - 1) {
        return Endianness::BIG;
    }

    // Ambiguous - check if difference is significant
    double ratio = static_cast<double>(byte_changes[0]) /
                   std::max(static_cast<size_t>(1), byte_changes[bytes_per_sample - 1]);

    if (ratio > 1.3) {
        return Endianness::LITTLE;
    } else if (ratio < 0.77) {
        return Endianness::BIG;
    }

    // Default to big-endian for backwards compatibility
    return Endianness::BIG;
}

int AudioFormatProbe::estimate_sample_rate(int channels, int bit_depth) const {
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        last_packet_time_ - first_packet_time_);

    if (duration.count() <= 0) {
        return 48000;  // Default
    }

    int bytes_per_frame = channels * (bit_depth / 8);
    double seconds = duration.count() / 1000.0;
    double frames_per_second = (total_bytes_received_ / bytes_per_frame) / seconds;

    return round_to_common_sample_rate(static_cast<int>(frames_per_second + 0.5));
}

int AudioFormatProbe::round_to_common_sample_rate(int raw_rate) {
    int closest = 48000;
    int min_diff = std::abs(raw_rate - 48000);

    for (int rate : kCommonSampleRates) {
        int diff = std::abs(raw_rate - rate);
        if (diff < min_diff) {
            min_diff = diff;
            closest = rate;
        }
    }

    return closest;
}

StreamCodec AudioFormatProbe::detect_codec() const {
    if (probe_buffer_.size() < 1000) {
        return StreamCodec::UNKNOWN;
    }

    // Try all codecs and score by discontinuity (lower = better)
    struct CodecScore {
        StreamCodec codec;
        double score;
    };
    
    std::vector<CodecScore> scores;
    
    // Score PCMU
    double ulaw_score = compute_ulaw_discontinuity();
    if (ulaw_score < std::numeric_limits<double>::max() / 2) {
        scores.push_back({StreamCodec::PCMU, ulaw_score});
    }
    
    // Score PCMA
    double alaw_score = compute_alaw_discontinuity();
    if (alaw_score < std::numeric_limits<double>::max() / 2) {
        scores.push_back({StreamCodec::PCMA, alaw_score});
    }
    
    // Score Opus
    double opus_score = compute_opus_discontinuity();
    if (opus_score < std::numeric_limits<double>::max() / 2) {
        scores.push_back({StreamCodec::OPUS, opus_score});
    }
    
    // Score MP3
    // TODO: MP3 detection disabled until hip decoder integration is verified
    // double mp3_score = compute_mp3_discontinuity();
    // if (mp3_score < std::numeric_limits<double>::max() / 2) {
    //     scores.push_back({StreamCodec::MP3, mp3_score});
    // }

    // Compare against BEST PCM interpretation (try multiple formats)
    // This prevents false positives when data is actually PCM
    double best_pcm_score = std::numeric_limits<double>::max();
    const int pcm_channels[] = {1, 2};
    const int pcm_bits[] = {8, 16};
    for (int ch : pcm_channels) {
        for (int bits : pcm_bits) {
            Endianness endian = (bits == 8) ? Endianness::LITTLE : detect_endianness(bits);
            double pcm_score = compute_discontinuity_score(ch, bits, endian);
            if (pcm_score < best_pcm_score) {
                best_pcm_score = pcm_score;
            }
        }
    }
    
    if (scores.empty()) {
        return StreamCodec::UNKNOWN;
    }
    
    // Find best (lowest) score among coded formats
    auto best = std::min_element(scores.begin(), scores.end(),
        [](const CodecScore& a, const CodecScore& b) { return a.score < b.score; });
    
    // Need significant improvement over best PCM to declare coded format
    // If best PCM is smoother or similar, prefer PCM (return UNKNOWN)
    constexpr double kImprovementThreshold = 0.5;  // Coded score must be < 50% of best PCM
    
    if (best_pcm_score > 0 && best->score / best_pcm_score < kImprovementThreshold) {
        return best->codec;
    }

    return StreamCodec::UNKNOWN;
}

double AudioFormatProbe::compute_ulaw_discontinuity() const {
    // μ-law to linear expansion table (ITU-T G.711)
    static const int16_t ulaw_table[256] = {
        -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,
        -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
        -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,
        -11900,-11388,-10876,-10364,-9852,-9340,-8828,-8316,
        -7932,-7676,-7420,-7164,-6908,-6652,-6396,-6140,
        -5884,-5628,-5372,-5116,-4860,-4604,-4348,-4092,
        -3900,-3772,-3644,-3516,-3388,-3260,-3132,-3004,
        -2876,-2748,-2620,-2492,-2364,-2236,-2108,-1980,
        -1884,-1820,-1756,-1692,-1628,-1564,-1500,-1436,
        -1372,-1308,-1244,-1180,-1116,-1052,-988,-924,
        -876,-844,-812,-780,-748,-716,-684,-652,
        -620,-588,-556,-524,-492,-460,-428,-396,
        -372,-356,-340,-324,-308,-292,-276,-260,
        -244,-228,-212,-196,-180,-164,-148,-132,
        -120,-112,-104,-96,-88,-80,-72,-64,
        -56,-48,-40,-32,-24,-16,-8,0,
        32124,31100,30076,29052,28028,27004,25980,24956,
        23932,22908,21884,20860,19836,18812,17788,16764,
        15996,15484,14972,14460,13948,13436,12924,12412,
        11900,11388,10876,10364,9852,9340,8828,8316,
        7932,7676,7420,7164,6908,6652,6396,6140,
        5884,5628,5372,5116,4860,4604,4348,4092,
        3900,3772,3644,3516,3388,3260,3132,3004,
        2876,2748,2620,2492,2364,2236,2108,1980,
        1884,1820,1756,1692,1628,1564,1500,1436,
        1372,1308,1244,1180,1116,1052,988,924,
        876,844,812,780,748,716,684,652,
        620,588,556,524,492,460,428,396,
        372,356,340,324,308,292,276,260,
        244,228,212,196,180,164,148,132,
        120,112,104,96,88,80,72,64,
        56,48,40,32,24,16,8,0
    };

    if (probe_buffer_.size() < 100) {
        return std::numeric_limits<double>::max();
    }

    double total_discontinuity = 0.0;
    int16_t prev_sample = ulaw_table[probe_buffer_[0]];
    
    for (size_t i = 1; i < probe_buffer_.size(); ++i) {
        int16_t sample = ulaw_table[probe_buffer_[i]];
        int32_t diff = std::abs(static_cast<int32_t>(sample) - prev_sample);
        if (diff > 6500) {
            total_discontinuity += static_cast<double>(diff) / 32767.0;
        }
        prev_sample = sample;
    }

    return total_discontinuity / probe_buffer_.size();
}

double AudioFormatProbe::compute_alaw_discontinuity() const {
    // A-law to linear expansion table (ITU-T G.711)
    static const int16_t alaw_table[256] = {
        -5504,-5248,-6016,-5760,-4480,-4224,-4992,-4736,
        -7552,-7296,-8064,-7808,-6528,-6272,-7040,-6784,
        -2752,-2624,-3008,-2880,-2240,-2112,-2496,-2368,
        -3776,-3648,-4032,-3904,-3264,-3136,-3520,-3392,
        -22016,-20992,-24064,-23040,-17920,-16896,-19968,-18944,
        -30208,-29184,-32256,-31232,-26112,-25088,-28160,-27136,
        -11008,-10496,-12032,-11520,-8960,-8448,-9984,-9472,
        -15104,-14592,-16128,-15616,-13056,-12544,-14080,-13568,
        -344,-328,-376,-360,-280,-264,-312,-296,
        -472,-456,-504,-488,-408,-392,-440,-424,
        -88,-72,-120,-104,-24,-8,-56,-40,
        -216,-200,-248,-232,-152,-136,-184,-168,
        -1376,-1312,-1504,-1440,-1120,-1056,-1248,-1184,
        -1888,-1824,-2016,-1952,-1632,-1568,-1760,-1696,
        -688,-656,-752,-720,-560,-528,-624,-592,
        -944,-912,-1008,-976,-816,-784,-880,-848,
        5504,5248,6016,5760,4480,4224,4992,4736,
        7552,7296,8064,7808,6528,6272,7040,6784,
        2752,2624,3008,2880,2240,2112,2496,2368,
        3776,3648,4032,3904,3264,3136,3520,3392,
        22016,20992,24064,23040,17920,16896,19968,18944,
        30208,29184,32256,31232,26112,25088,28160,27136,
        11008,10496,12032,11520,8960,8448,9984,9472,
        15104,14592,16128,15616,13056,12544,14080,13568,
        344,328,376,360,280,264,312,296,
        472,456,504,488,408,392,440,424,
        88,72,120,104,24,8,56,40,
        216,200,248,232,152,136,184,168,
        1376,1312,1504,1440,1120,1056,1248,1184,
        1888,1824,2016,1952,1632,1568,1760,1696,
        688,656,752,720,560,528,624,592,
        944,912,1008,976,816,784,880,848
    };

    if (probe_buffer_.size() < 100) {
        return std::numeric_limits<double>::max();
    }

    double total_discontinuity = 0.0;
    int16_t prev_sample = alaw_table[probe_buffer_[0]];
    
    for (size_t i = 1; i < probe_buffer_.size(); ++i) {
        int16_t sample = alaw_table[probe_buffer_[i]];
        int32_t diff = std::abs(static_cast<int32_t>(sample) - prev_sample);
        if (diff > 6500) {
            total_discontinuity += static_cast<double>(diff) / 32767.0;
        }
        prev_sample = sample;
    }

    return total_discontinuity / probe_buffer_.size();
}

double AudioFormatProbe::compute_opus_discontinuity() const {
    // Try decoding buffer as Opus audio
    // Opus frames are self-delimiting, so we try decoding the buffer
    
    if (probe_buffer_.size() < 100) {
        return std::numeric_limits<double>::max();
    }

    int error = 0;
    OpusDecoder* decoder = opus_decoder_create(48000, 2, &error);
    if (error != OPUS_OK || !decoder) {
        return std::numeric_limits<double>::max();
    }

    // Try to decode the first chunk as an Opus frame
    // Opus frames are typically 20-60ms, so ~960-2880 samples at 48kHz stereo
    constexpr int kMaxFrameSamples = 5760;  // 120ms max at 48kHz
    std::vector<int16_t> pcm(kMaxFrameSamples * 2);  // stereo
    
    int decoded_samples = opus_decode(decoder, 
                                       probe_buffer_.data(), 
                                       static_cast<opus_int32>(std::min(probe_buffer_.size(), size_t(2000))),
                                       pcm.data(), 
                                       kMaxFrameSamples, 
                                       0);  // 0 = no FEC
    
    opus_decoder_destroy(decoder);
    
    if (decoded_samples <= 0) {
        // Decode failed - not Opus
        return std::numeric_limits<double>::max();
    }

    // Compute discontinuity on decoded PCM
    double total_discontinuity = 0.0;
    for (int i = 1; i < decoded_samples * 2; ++i) {  // *2 for stereo
        int32_t diff = std::abs(static_cast<int32_t>(pcm[i]) - pcm[i-1]);
        if (diff > 6500) {
            total_discontinuity += static_cast<double>(diff) / 32767.0;
        }
    }

    return total_discontinuity / (decoded_samples * 2);
}

double AudioFormatProbe::compute_mp3_discontinuity() const {
    // Try decoding buffer as MP3 audio using hip (LAME decoder)
    
    if (probe_buffer_.size() < 1000) {
        return std::numeric_limits<double>::max();
    }

    hip_t hip = hip_decode_init();
    if (!hip) {
        return std::numeric_limits<double>::max();
    }

    // Decode buffer - hip needs enough data for frame sync
    constexpr int kMaxSamples = 8192;
    std::vector<short> pcm_left(kMaxSamples);
    std::vector<short> pcm_right(kMaxSamples);
    
    int decoded_samples = hip_decode(hip,
                                     const_cast<unsigned char*>(probe_buffer_.data()),
                                     probe_buffer_.size(),
                                     pcm_left.data(),
                                     pcm_right.data());
    
    hip_decode_exit(hip);
    
    if (decoded_samples <= 0) {
        // Decode failed - not MP3 or invalid data
        return std::numeric_limits<double>::max();
    }

    // Compute discontinuity on decoded PCM (just left channel)
    double total_discontinuity = 0.0;
    for (int i = 1; i < decoded_samples; ++i) {
        int32_t diff = std::abs(static_cast<int32_t>(pcm_left[i]) - pcm_left[i-1]);
        if (diff > 6500) {
            total_discontinuity += static_cast<double>(diff) / 32767.0;
        }
    }

    return total_discontinuity / decoded_samples;
}

}  // namespace audio
}  // namespace screamrouter
