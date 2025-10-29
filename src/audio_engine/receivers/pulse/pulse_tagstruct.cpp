#include "pulse_tagstruct.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <cstring>
#include <stdexcept>

namespace screamrouter {
namespace audio {
namespace pulse {

namespace {

bool read_u8_internal(const uint8_t* data, size_t length, size_t& index, uint8_t& value) {
    if (index + 1 > length) {
        return false;
    }
    value = data[index++];
    return true;
}

bool read_u32_internal(const uint8_t* data, size_t length, size_t& index, uint32_t& value) {
    if (index + 4 > length) {
        return false;
    }
    uint32_t net;
    std::memcpy(&net, data + index, sizeof(net));
    index += 4;
    value = ntohl(net);
    return true;
}

bool read_u64_internal(const uint8_t* data, size_t length, size_t& index, uint64_t& value) {
    uint32_t hi = 0;
    uint32_t lo = 0;
    if (!read_u32_internal(data, length, index, hi)) {
        return false;
    }
    if (!read_u32_internal(data, length, index, lo)) {
        return false;
    }
    value = (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
    return true;
}

} // namespace

TagReader::TagReader(const uint8_t* data, size_t length)
    : data_(data), length_(length) {}

bool TagReader::read_tag(Tag expected) {
    if (index_ >= length_) {
        return false;
    }
    uint8_t tag_value = data_[index_++];
    if (expected == Tag::Boolean) {
        return tag_value == static_cast<uint8_t>(Tag::BooleanTrue) ||
               tag_value == static_cast<uint8_t>(Tag::BooleanFalse);
    }
    return tag_value == static_cast<uint8_t>(expected);
}

bool TagReader::read_bytes(void* out, size_t n) {
    if (index_ + n > length_) {
        return false;
    }
    std::memcpy(out, data_ + index_, n);
    index_ += n;
    return true;
}

std::optional<uint32_t> TagReader::read_u32() {
    if (!read_tag(Tag::U32)) {
        return std::nullopt;
    }
    uint32_t value = 0;
    if (!read_u32_internal(data_, length_, index_, value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<uint8_t> TagReader::read_u8() {
    if (!read_tag(Tag::U8)) {
        return std::nullopt;
    }
    uint8_t value = 0;
    if (!read_u8_internal(data_, length_, index_, value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<uint64_t> TagReader::read_u64() {
    if (!read_tag(Tag::U64)) {
        return std::nullopt;
    }
    uint64_t value = 0;
    if (!read_u64_internal(data_, length_, index_, value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<int64_t> TagReader::read_s64() {
    if (!read_tag(Tag::S64)) {
        return std::nullopt;
    }
    uint64_t raw = 0;
    if (!read_u64_internal(data_, length_, index_, raw)) {
        return std::nullopt;
    }
    return static_cast<int64_t>(raw);
}

std::optional<uint64_t> TagReader::read_usec() {
    if (!read_tag(Tag::Usec)) {
        return std::nullopt;
    }
    uint64_t value = 0;
    if (!read_u64_internal(data_, length_, index_, value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<timeval> TagReader::read_timeval() {
    if (!read_tag(Tag::Timeval)) {
        return std::nullopt;
    }
    uint32_t sec = 0;
    uint32_t usec = 0;
    if (!read_u32_internal(data_, length_, index_, sec)) {
        return std::nullopt;
    }
    if (!read_u32_internal(data_, length_, index_, usec)) {
        return std::nullopt;
    }
    timeval tv{};
#if defined(_WIN32)
    tv.tv_sec = static_cast<long>(sec);
    tv.tv_usec = static_cast<long>(usec);
#else
    tv.tv_sec = static_cast<time_t>(sec);
    tv.tv_usec = static_cast<suseconds_t>(usec);
#endif
    return tv;
}

std::optional<std::string> TagReader::read_string() {
    if (index_ >= length_) {
        return std::nullopt;
    }
    uint8_t tag_value = data_[index_++];
    if (tag_value == static_cast<uint8_t>(Tag::StringNull)) {
        return std::string();
    }
    if (tag_value != static_cast<uint8_t>(Tag::String)) {
        return std::nullopt;
    }

    // Read until null terminator.
    size_t start = index_;
    while (index_ < length_) {
        if (data_[index_] == 0) {
            std::string result(reinterpret_cast<const char*>(data_ + start));
            ++index_; // Skip null terminator.
            return result;
        }
        ++index_;
    }
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> TagReader::read_arbitrary() {
    if (!read_tag(Tag::Arbitrary)) {
        return std::nullopt;
    }
    uint32_t len = 0;
    if (!read_u32_internal(data_, length_, index_, len)) {
        return std::nullopt;
    }
    if (index_ + len > length_) {
        return std::nullopt;
    }
    std::vector<uint8_t> result(len);
    if (len > 0) {
        std::memcpy(result.data(), data_ + index_, len);
        index_ += len;
    }
    return result;
}

std::optional<bool> TagReader::read_bool() {
    if (index_ >= length_) {
        return std::nullopt;
    }
    uint8_t tag_value = data_[index_++];
    if (tag_value == static_cast<uint8_t>(Tag::BooleanTrue)) {
        return true;
    }
    if (tag_value == static_cast<uint8_t>(Tag::BooleanFalse)) {
        return false;
    }
    return std::nullopt;
}

std::optional<SampleSpec> TagReader::read_sample_spec() {
    if (!read_tag(Tag::SampleSpec)) {
        return std::nullopt;
    }
    SampleSpec spec;
    if (!read_u8_internal(data_, length_, index_, spec.format)) {
        return std::nullopt;
    }
    if (!read_u8_internal(data_, length_, index_, spec.channels)) {
        return std::nullopt;
    }
    if (!read_u32_internal(data_, length_, index_, spec.rate)) {
        return std::nullopt;
    }
    return spec;
}

std::optional<ChannelMap> TagReader::read_channel_map() {
    if (!read_tag(Tag::ChannelMap)) {
        return std::nullopt;
    }
    ChannelMap map;
    if (!read_u8_internal(data_, length_, index_, map.channels)) {
        return std::nullopt;
    }
    map.map.resize(map.channels);
    for (uint8_t& entry : map.map) {
        if (!read_u8_internal(data_, length_, index_, entry)) {
            return std::nullopt;
        }
    }
    return map;
}

std::optional<CVolume> TagReader::read_cvolume() {
    if (!read_tag(Tag::CVolume)) {
        return std::nullopt;
    }
    CVolume volume;
    if (!read_u8_internal(data_, length_, index_, volume.channels)) {
        return std::nullopt;
    }
    volume.values.resize(volume.channels);
    for (uint32_t& v : volume.values) {
        if (!read_u32_internal(data_, length_, index_, v)) {
            return std::nullopt;
        }
    }
    return volume;
}

std::optional<Proplist> TagReader::read_proplist() {
    if (!read_tag(Tag::Proplist)) {
        return std::nullopt;
    }
    Proplist props;
    while (true) {
        auto key = read_string();
        if (!key.has_value()) {
            return std::nullopt;
        }
        if (key->empty()) {
            break; // Terminator.
        }
        auto length_tag = read_u32();
        if (!length_tag.has_value()) {
            return std::nullopt;
        }
        uint32_t len = *length_tag;
        auto blob = read_arbitrary();
        if (!blob || blob->size() != len) {
            throw std::runtime_error("Proplist data length mismatch");
        }
        std::string value(reinterpret_cast<const char*>(blob->data()), blob->size());
        props.emplace(std::move(*key), std::move(value));
    }
    return props;
}

void TagReader::skip_remaining() {
    index_ = length_;
}

void TagWriter::put_tag(Tag tag) {
    buffer_.push_back(static_cast<uint8_t>(tag));
}

void TagWriter::put_u32_raw(uint32_t value) {
    uint32_t net = htonl(value);
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&net);
    buffer_.insert(buffer_.end(), ptr, ptr + sizeof(net));
}

void TagWriter::put_u32(uint32_t value) {
    put_tag(Tag::U32);
    put_u32_raw(value);
}

void TagWriter::put_u8(uint8_t value) {
    put_tag(Tag::U8);
    buffer_.push_back(value);
}

void TagWriter::put_u64(uint64_t value) {
    put_tag(Tag::U64);
    uint32_t hi = static_cast<uint32_t>(value >> 32);
    uint32_t lo = static_cast<uint32_t>(value & 0xFFFFFFFFULL);
    put_u32_raw(hi);
    put_u32_raw(lo);
}

void TagWriter::put_s64(int64_t value) {
    put_tag(Tag::S64);
    uint64_t raw = static_cast<uint64_t>(value);
    uint32_t hi = static_cast<uint32_t>(raw >> 32);
    uint32_t lo = static_cast<uint32_t>(raw & 0xFFFFFFFFULL);
    put_u32_raw(hi);
    put_u32_raw(lo);
}

void TagWriter::put_string(const std::string& value) {
    put_tag(Tag::String);
    buffer_.insert(buffer_.end(), value.begin(), value.end());
    buffer_.push_back('\0');
}

void TagWriter::put_nullable_string(const std::string* value) {
    if (value) {
        put_string(*value);
    } else {
        put_tag(Tag::StringNull);
    }
}

void TagWriter::put_arbitrary(const uint8_t* data, size_t length) {
    put_tag(Tag::Arbitrary);
    put_u32_raw(static_cast<uint32_t>(length));
    if (length > 0) {
        buffer_.insert(buffer_.end(), data, data + length);
    }
}

void TagWriter::put_bool(bool value) {
    put_tag(value ? Tag::BooleanTrue : Tag::BooleanFalse);
}

void TagWriter::put_sample_spec(const SampleSpec& spec) {
    put_tag(Tag::SampleSpec);
    buffer_.push_back(spec.format);
    buffer_.push_back(spec.channels);
    put_u32_raw(spec.rate);
}

void TagWriter::put_channel_map(const ChannelMap& map) {
    put_tag(Tag::ChannelMap);
    buffer_.push_back(map.channels);
    buffer_.insert(buffer_.end(), map.map.begin(), map.map.end());
}

void TagWriter::put_cvolume(const CVolume& volume) {
    put_tag(Tag::CVolume);
    buffer_.push_back(volume.channels);
    for (uint32_t v : volume.values) {
        put_u32_raw(v);
    }
}

void TagWriter::put_volume(uint32_t value) {
    put_tag(Tag::Volume);
    put_u32_raw(value);
}

void TagWriter::put_format_info(uint8_t encoding, const Proplist& plist) {
    put_tag(Tag::FormatInfo);
    put_u8(encoding);
    put_proplist(plist);
}

void TagWriter::put_proplist(const Proplist& plist) {
    put_tag(Tag::Proplist);
    for (const auto& [key, value] : plist) {
        put_string(key);
        put_u32(static_cast<uint32_t>(value.size()));
        const auto* bytes = reinterpret_cast<const uint8_t*>(value.data());
        put_arbitrary(bytes, value.size());
    }
    put_nullable_string(nullptr); // terminator
}

void TagWriter::put_usec(uint64_t value) {
    put_tag(Tag::Usec);
    uint32_t hi = static_cast<uint32_t>(value >> 32);
    uint32_t lo = static_cast<uint32_t>(value & 0xFFFFFFFFULL);
    put_u32_raw(hi);
    put_u32_raw(lo);
}

void TagWriter::put_timeval(const timeval& tv) {
    put_tag(Tag::Timeval);
    put_u32_raw(static_cast<uint32_t>(tv.tv_sec));
    put_u32_raw(static_cast<uint32_t>(tv.tv_usec));
}

void TagWriter::put_command(Command command, uint32_t tag) {
    put_u32(static_cast<uint32_t>(command));
    put_u32(tag);
}

} // namespace pulse
} // namespace audio
} // namespace screamrouter
