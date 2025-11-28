#ifndef SCREAMROUTER_AUDIO_PULSE_PULSE_TAGSTRUCT_H
#define SCREAMROUTER_AUDIO_PULSE_PULSE_TAGSTRUCT_H

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32)
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

#include "pulse_protocol.h"

namespace screamrouter {
namespace audio {
namespace pulse {

// Subset of PulseAudio tag identifiers used for command payloads.
enum class Tag : uint8_t {
    Invalid = 0,
    String = 't',
    StringNull = 'N',
    U32 = 'L',
    U8 = 'B',
    U64 = 'R',
    S64 = 'r',
    SampleSpec = 'a',
    Arbitrary = 'x',
    BooleanTrue = '1',
    BooleanFalse = '0',
    Boolean = BooleanTrue,
    Timeval = 'T',
    Usec = 'U',
    ChannelMap = 'm',
    CVolume = 'v',
    Proplist = 'P',
    Volume = 'V',
    FormatInfo = 'f'
};

struct SampleSpec {
    uint8_t format = 0;    // pa_sample_format_t
    uint8_t channels = 0;
    uint32_t rate = 0;
};

struct ChannelMap {
    uint8_t channels = 0;
    std::vector<uint8_t> map; // pa_channel_position_t values
};

struct CVolume {
    uint8_t channels = 0;
    std::vector<uint32_t> values;
};

using Proplist = std::unordered_map<std::string, std::string>;

class TagReader {
public:
    TagReader(const uint8_t* data, size_t length);

    bool eof() const { return index_ >= length_; }
    bool empty() const { return eof(); }

    std::optional<uint32_t> read_u32();
    std::optional<uint8_t> read_u8();
    std::optional<timeval> read_timeval();
    std::optional<std::string> read_string();
    std::optional<std::vector<uint8_t>> read_arbitrary();
    std::optional<bool> read_bool();
    std::optional<SampleSpec> read_sample_spec();
    std::optional<ChannelMap> read_channel_map();
    std::optional<CVolume> read_cvolume();
    std::optional<Proplist> read_proplist();

    size_t bytes_consumed() const { return index_; }
    const uint8_t* current_data() const { return data_ + index_; }
    size_t bytes_remaining() const { return length_ - index_; }
    void skip_remaining();

private:
    bool read_tag(Tag expected);

    const uint8_t* data_ = nullptr;
    size_t length_ = 0;
    size_t index_ = 0;
};

class TagWriter {
public:
    void put_u32(uint32_t value);
    void put_u8(uint8_t value);
    void put_u64(uint64_t value);
    void put_s64(int64_t value);
    void put_string(const std::string& value);
    void put_nullable_string(const std::string* value);
    void put_arbitrary(const uint8_t* data, size_t length);
    void put_bool(bool value);
    void put_sample_spec(const SampleSpec& spec);
    void put_channel_map(const ChannelMap& map);
    void put_cvolume(const CVolume& volume);
    void put_volume(uint32_t value);
    void put_format_info(uint8_t encoding, const Proplist& plist);
    void put_proplist(const Proplist& plist);
    void put_usec(uint64_t value);
    void put_timeval(const timeval& tv);

    void put_command(Command command, uint32_t tag);

    const std::vector<uint8_t>& buffer() const { return buffer_; }

private:
    void put_tag(Tag tag);
    void put_u32_raw(uint32_t value);

    std::vector<uint8_t> buffer_;
};

} // namespace pulse
} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_PULSE_PULSE_TAGSTRUCT_H
