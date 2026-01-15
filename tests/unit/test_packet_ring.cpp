#include <gtest/gtest.h>
#include "utils/packet_ring.h"

using screamrouter::audio::utils::PacketRing;

class PacketRingTest : public ::testing::Test {
protected:
    PacketRing<int> ring{4};  // Capacity 4
};

TEST_F(PacketRingTest, InitialState) {
    EXPECT_EQ(ring.size(), 0u);
    EXPECT_EQ(ring.capacity(), 4u);
    EXPECT_EQ(ring.drop_count(), 0u);
}

TEST_F(PacketRingTest, PushAndPop) {
    EXPECT_TRUE(ring.push(10));
    EXPECT_TRUE(ring.push(20));
    EXPECT_EQ(ring.size(), 2u);
    
    int value = 0;
    EXPECT_TRUE(ring.pop(value));
    EXPECT_EQ(value, 10);
    EXPECT_TRUE(ring.pop(value));
    EXPECT_EQ(value, 20);
    EXPECT_EQ(ring.size(), 0u);
}

TEST_F(PacketRingTest, PopEmpty) {
    int value = -1;
    EXPECT_FALSE(ring.pop(value));
    EXPECT_EQ(value, -1);  // Unchanged
}

TEST_F(PacketRingTest, DropOldestOnOverflow) {
    // Fill to capacity-1 (ring uses one slot as sentinel)
    ring.push(1);
    ring.push(2);
    ring.push(3);
    EXPECT_EQ(ring.size(), 3u);
    EXPECT_EQ(ring.drop_count(), 0u);
    
    // This should cause overflow and drop oldest
    ring.push(4);
    EXPECT_EQ(ring.size(), 3u);  // Still 3 (capacity-1)
    EXPECT_EQ(ring.drop_count(), 1u);
    
    // Verify oldest (1) was dropped
    int value;
    ring.pop(value);
    EXPECT_EQ(value, 2);  // Not 1
    ring.pop(value);
    EXPECT_EQ(value, 3);
    ring.pop(value);
    EXPECT_EQ(value, 4);
}

TEST_F(PacketRingTest, MultipleOverflows) {
    // Push more items than capacity
    for (int i = 0; i < 10; ++i) {
        ring.push(i);
    }
    
    // Should have dropped 7 items (10 pushed, capacity-1 = 3 remain)
    EXPECT_EQ(ring.drop_count(), 7u);
    EXPECT_EQ(ring.size(), 3u);
    
    // Remaining items should be the last 3 pushed
    int value;
    ring.pop(value);
    EXPECT_EQ(value, 7);
    ring.pop(value);
    EXPECT_EQ(value, 8);
    ring.pop(value);
    EXPECT_EQ(value, 9);
}

TEST_F(PacketRingTest, MoveSemantics) {
    PacketRing<std::string> str_ring{3};
    
    std::string s = "hello";
    str_ring.push(std::move(s));
    EXPECT_TRUE(s.empty());  // Was moved
    
    std::string out;
    str_ring.pop(out);
    EXPECT_EQ(out, "hello");
}

TEST_F(PacketRingTest, FIFOOrder) {
    ring.push(100);
    ring.push(200);
    ring.push(300);
    
    int v1, v2, v3;
    ring.pop(v1);
    ring.pop(v2);
    ring.pop(v3);
    
    EXPECT_EQ(v1, 100);
    EXPECT_EQ(v2, 200);
    EXPECT_EQ(v3, 300);
}
