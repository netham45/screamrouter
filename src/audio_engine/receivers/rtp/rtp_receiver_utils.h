/**
 * @file rtp_receiver_utils.h
 */
#ifndef RTP_RECEIVER_UTILS_H
#define RTP_RECEIVER_UTILS_H

#include <cstdint>
#include <string>
#include <vector>

namespace screamrouter {
namespace audio {

bool is_system_little_endian();
void swap_endianness(uint8_t* data, size_t size, int bit_depth);
int16_t decode_mulaw_sample(uint8_t value);
std::string sanitize_tag(const std::string& input);
bool resolve_opus_multistream_layout(
    int channels,
    int sample_rate,
    int mapping_family,
    int& streams,
    int& coupled_streams,
    std::vector<unsigned char>& mapping);

} // namespace audio
} // namespace screamrouter

#endif // RTP_RECEIVER_UTILS_H
