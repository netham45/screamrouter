#include <gtest/gtest.h>
#include "utils/byte_ring_buffer.h"

using screamrouter::audio::utils::ByteRingBuffer;

class ByteRingBufferTest : public ::testing::Test {
protected:
    ByteRingBuffer buffer;
};

TEST_F(ByteRingBufferTest, InitiallyEmpty) {
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);
}

TEST_F(ByteRingBufferTest, WriteAndRead) {
    uint8_t data[] = {1, 2, 3, 4, 5};
    buffer.write(data, sizeof(data));
    
    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(buffer.size(), 5u);
    
    uint8_t out[5] = {0};
    size_t read = buffer.pop(out, sizeof(out));
    
    EXPECT_EQ(read, 5u);
    EXPECT_TRUE(buffer.empty());
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(out[i], data[i]);
    }
}

TEST_F(ByteRingBufferTest, PartialRead) {
    uint8_t data[] = {1, 2, 3, 4, 5};
    buffer.write(data, sizeof(data));
    
    uint8_t out[3] = {0};
    size_t read = buffer.pop(out, 3);
    
    EXPECT_EQ(read, 3u);
    EXPECT_EQ(buffer.size(), 2u);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], 2);
    EXPECT_EQ(out[2], 3);
}

TEST_F(ByteRingBufferTest, Wraparound) {
    buffer.reserve(8);
    
    // Write 6 bytes
    uint8_t data1[] = {1, 2, 3, 4, 5, 6};
    buffer.write(data1, sizeof(data1));
    
    // Pop 4 bytes (head advances)
    uint8_t out1[4];
    buffer.pop(out1, 4);
    EXPECT_EQ(buffer.size(), 2u);
    
    // Write 5 more bytes (should wrap around)
    uint8_t data2[] = {7, 8, 9, 10, 11};
    buffer.write(data2, sizeof(data2));
    EXPECT_EQ(buffer.size(), 7u);
    
    // Read all 7 bytes
    uint8_t out2[7];
    size_t read = buffer.pop(out2, 7);
    EXPECT_EQ(read, 7u);
    
    // Verify order: 5, 6 (remaining from first write), 7, 8, 9, 10, 11
    EXPECT_EQ(out2[0], 5);
    EXPECT_EQ(out2[1], 6);
    EXPECT_EQ(out2[2], 7);
    EXPECT_EQ(out2[3], 8);
    EXPECT_EQ(out2[4], 9);
    EXPECT_EQ(out2[5], 10);
    EXPECT_EQ(out2[6], 11);
}

TEST_F(ByteRingBufferTest, CapacityGrowth) {
    // Start with small buffer
    buffer.reserve(4);
    EXPECT_GE(buffer.capacity(), 4u);
    
    // Write more than initial capacity
    uint8_t data[10];
    for (int i = 0; i < 10; ++i) data[i] = static_cast<uint8_t>(i);
    buffer.write(data, 10);
    
    EXPECT_EQ(buffer.size(), 10u);
    EXPECT_GE(buffer.capacity(), 10u);
    
    uint8_t out[10];
    buffer.pop(out, 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(out[i], i);
    }
}

TEST_F(ByteRingBufferTest, Clear) {
    uint8_t data[] = {1, 2, 3};
    buffer.write(data, sizeof(data));
    EXPECT_EQ(buffer.size(), 3u);
    
    buffer.clear();
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);
}

TEST_F(ByteRingBufferTest, PopMoreThanAvailable) {
    uint8_t data[] = {1, 2, 3};
    buffer.write(data, sizeof(data));
    
    uint8_t out[10] = {0};
    size_t read = buffer.pop(out, 10);
    
    EXPECT_EQ(read, 3u);  // Only 3 bytes available
    EXPECT_TRUE(buffer.empty());
}

TEST_F(ByteRingBufferTest, WriteNullOrZero) {
    buffer.write(nullptr, 5);  // Should handle gracefully
    EXPECT_TRUE(buffer.empty());
    
    uint8_t data[] = {1, 2, 3};
    buffer.write(data, 0);  // Zero bytes
    EXPECT_TRUE(buffer.empty());
}

TEST_F(ByteRingBufferTest, PopNullOrZero) {
    uint8_t data[] = {1, 2, 3};
    buffer.write(data, sizeof(data));
    
    size_t read = buffer.pop(nullptr, 5);  // Null dest
    EXPECT_EQ(read, 0u);
    EXPECT_EQ(buffer.size(), 3u);  // Unchanged
    
    uint8_t out[3];
    read = buffer.pop(out, 0);  // Zero bytes
    EXPECT_EQ(read, 0u);
    EXPECT_EQ(buffer.size(), 3u);  // Unchanged
}
