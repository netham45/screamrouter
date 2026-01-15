#include "rtp_receiver_utils.h"

#include "../../utils/cpp_logger.h"

#include <opus/opus.h>
#include <opus/opus_multistream.h>

#include <algorithm>
#include <cctype>

namespace screamrouter {
namespace audio {

namespace {
constexpr int kMuLawBias = 0x84;
} // namespace

bool is_system_little_endian() {
    int n = 1;
    return (*(char *)&n == 1);
}

void swap_endianness(uint8_t* data, size_t size, int bit_depth) {
    if (bit_depth == 16) {
        for (size_t i = 0; i + 1 < size; i += 2) {
            std::swap(data[i], data[i + 1]);
        }
    } else if (bit_depth == 24) {
        for (size_t i = 0; i + 2 < size; i += 3) {
            std::swap(data[i], data[i + 2]);
        }
    } else if (bit_depth == 32) {
        for (size_t i = 0; i + 3 < size; i += 4) {
            std::swap(data[i], data[i + 3]);
            std::swap(data[i + 1], data[i + 2]);
        }
    }
}

int16_t decode_mulaw_sample(uint8_t value) {
    value = static_cast<uint8_t>(~value);
    const int sign = value & 0x80;
    const int exponent = (value >> 4) & 0x07;
    const int mantissa = value & 0x0F;
    int sample = ((mantissa << 3) + kMuLawBias) << exponent;
    sample -= kMuLawBias;
    return static_cast<int16_t>(sign ? -sample : sample);
}

int16_t decode_alaw_sample(uint8_t value) {
    value ^= 0x55;

    int16_t t = (value & 0x0F) << 4;
    int16_t segment = (value & 0x70) >> 4;

    switch (segment) {
        case 0:
            t += 8;
            break;
        case 1:
            t += 0x108;
            break;
        default:
            t += 0x108;
            t <<= (segment - 1);
            break;
    }

    return (value & 0x80) ? t : static_cast<int16_t>(-t);
}

std::string sanitize_tag(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    char last = '\0';
    for (char c : input) {
        char lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (std::isalnum(static_cast<unsigned char>(lowered))) {
            out.push_back(lowered);
            last = lowered;
        } else if (c == '-' || c == '_') {
            out.push_back(c);
            last = c;
        } else if (last != '_') {
            out.push_back('_');
            last = '_';
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

bool resolve_opus_multistream_layout(int channels,
                                     int sample_rate,
                                     int mapping_family,
                                     int& streams,
                                     int& coupled_streams,
                                     std::vector<unsigned char>& mapping) {
    mapping.clear();

    if (channels <= 0) {
        return false;
    }

    if (channels <= 2) {
        if (channels == 1) {
            streams = 1;
            coupled_streams = 0;
            mapping = {0};
        } else {
            streams = 1;
            coupled_streams = 1;
            mapping = {0, 1};
        }
        return true;
    }

    std::vector<unsigned char> temp(static_cast<size_t>(channels), 0);
    int derived_streams = 0;
    int derived_coupled = 0;
    int error = OPUS_OK;
    OpusMSEncoder* probe = opus_multistream_surround_encoder_create(
        sample_rate,
        channels,
        mapping_family <= 0 ? 1 : mapping_family,
        &derived_streams,
        &derived_coupled,
        temp.data(),
        OPUS_APPLICATION_AUDIO,
        &error);

    if (error != OPUS_OK || !probe) {
        if (probe) {
            opus_multistream_encoder_destroy(probe);
        }
        LOG_CPP_ERROR("[RtpOpusReceiver] Failed to derive Opus layout for %d channels: %s", channels, opus_strerror(error));
        return false;
    }

    opus_multistream_encoder_destroy(probe);

    streams = derived_streams;
    coupled_streams = derived_coupled;
    mapping.assign(temp.begin(), temp.end());
    return true;
}

} // namespace audio
} // namespace screamrouter
