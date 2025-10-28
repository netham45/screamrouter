#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <cstring>

namespace screamrouter {
namespace audio {
namespace utils {

class ByteRingBuffer {
public:
    ByteRingBuffer() = default;

    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }
    std::size_t capacity() const { return buffer_.size(); }

    void reserve(std::size_t capacity) {
        ensure_capacity(capacity);
    }

    void clear() {
        head_ = 0;
        size_ = 0;
    }

    void write(const uint8_t* data, std::size_t bytes) {
        if (!data || bytes == 0) {
            return;
        }
        ensure_capacity(size_ + bytes);
        std::size_t tail = (head_ + size_) % buffer_.size();
        std::size_t first = std::min(bytes, buffer_.size() - tail);
        std::memcpy(buffer_.data() + tail, data, first);
        std::size_t remaining = bytes - first;
        if (remaining > 0) {
            std::memcpy(buffer_.data(), data + first, remaining);
        }
        size_ += bytes;
    }

    std::size_t pop(uint8_t* dest, std::size_t bytes) {
        if (!dest || bytes == 0 || size_ == 0) {
            return 0;
        }
        std::size_t to_read = std::min(bytes, size_);
        std::size_t first = std::min(to_read, buffer_.size() - head_);
        std::memcpy(dest, buffer_.data() + head_, first);
        std::size_t remaining = to_read - first;
        if (remaining > 0) {
            std::memcpy(dest + first, buffer_.data(), remaining);
        }
        head_ = (head_ + to_read) % buffer_.size();
        size_ -= to_read;
        return to_read;
    }

    const uint8_t* data_at(std::size_t offset) const {
        return buffer_.data() + ((head_ + offset) % buffer_.size());
    }

private:
    void ensure_capacity(std::size_t capacity) {
        if (capacity <= buffer_.size()) {
            return;
        }
        std::size_t new_capacity = buffer_.empty() ? capacity : buffer_.size();
        while (new_capacity < capacity) {
            new_capacity *= 2;
        }
        if (new_capacity == 0) {
            new_capacity = capacity;
        }
        std::vector<uint8_t> new_buffer(new_capacity);
        if (!buffer_.empty() && size_ > 0) {
            std::size_t first = std::min(size_, buffer_.size() - head_);
            std::memcpy(new_buffer.data(), buffer_.data() + head_, first);
            if (size_ > first) {
                std::memcpy(new_buffer.data() + first, buffer_.data(), size_ - first);
            }
        }
        buffer_.swap(new_buffer);
        head_ = 0;
    }

    std::vector<uint8_t> buffer_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};

} // namespace utils
} // namespace audio
} // namespace screamrouter
