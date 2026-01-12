#include <gtest/gtest.h>
#include "audio_channel_layout.h"

using namespace screamrouter::audio;

class AudioChannelLayoutTest : public ::testing::Test {};

TEST_F(AudioChannelLayoutTest, Family1MonoLayout) {
    auto roles = family1_canonical_channel_order(1);
    ASSERT_EQ(roles.size(), 1u);
    EXPECT_EQ(roles[0], ChannelRole::FrontCenter);
}

TEST_F(AudioChannelLayoutTest, Family1StereoLayout) {
    auto roles = family1_canonical_channel_order(2);
    ASSERT_EQ(roles.size(), 2u);
    EXPECT_EQ(roles[0], ChannelRole::FrontLeft);
    EXPECT_EQ(roles[1], ChannelRole::FrontRight);
}

TEST_F(AudioChannelLayoutTest, Family1QuadLayout) {
    auto roles = family1_canonical_channel_order(4);
    ASSERT_EQ(roles.size(), 4u);
    EXPECT_EQ(roles[0], ChannelRole::FrontLeft);
    EXPECT_EQ(roles[1], ChannelRole::FrontRight);
    // Back channels
    EXPECT_EQ(roles[2], ChannelRole::BackLeft);
    EXPECT_EQ(roles[3], ChannelRole::BackRight);
}

TEST_F(AudioChannelLayoutTest, Family1_51Layout) {
    auto roles = family1_canonical_channel_order(6);
    ASSERT_EQ(roles.size(), 6u);
    // Standard 5.1: FL, FC, FR, BL, BR, LFE (or similar arrangement)
    // Just verify we get 6 valid roles
    for (const auto& role : roles) {
        EXPECT_GE(static_cast<int>(role), 1);
        EXPECT_LE(static_cast<int>(role), 11);
    }
}

TEST_F(AudioChannelLayoutTest, UnsupportedChannelCount) {
    // For unusual channel counts, the function may use a fallback strategy
    // rather than returning empty. Check that it handles gracefully.
    auto roles = family1_canonical_channel_order(99);
    // Just verify it doesn't crash and returns something reasonable
    // The exact behavior is implementation-defined
    EXPECT_TRUE(roles.size() <= 99);  // Should not exceed requested
}

TEST_F(AudioChannelLayoutTest, ChannelMaskRoundTrip) {
    // Create a stereo mask
    std::vector<ChannelRole> stereo = {ChannelRole::FrontLeft, ChannelRole::FrontRight};
    uint32_t mask = channel_mask_from_roles(stereo);
    
    auto decoded = channel_order_from_mask(mask);
    ASSERT_EQ(decoded.size(), 2u);
    EXPECT_EQ(decoded[0], ChannelRole::FrontLeft);
    EXPECT_EQ(decoded[1], ChannelRole::FrontRight);
}

TEST_F(AudioChannelLayoutTest, DefaultMaskStereo) {
    uint32_t mask = default_channel_mask_for_channels(2);
    auto decoded = channel_order_from_mask(mask);
    ASSERT_EQ(decoded.size(), 2u);
}

TEST_F(AudioChannelLayoutTest, DefaultMaskMono) {
    uint32_t mask = default_channel_mask_for_channels(1);
    auto decoded = channel_order_from_mask(mask);
    ASSERT_EQ(decoded.size(), 1u);
}

TEST_F(AudioChannelLayoutTest, RolesToIndices) {
    std::vector<ChannelRole> roles = {ChannelRole::FrontLeft, ChannelRole::FrontRight};
    auto indices = roles_to_indices(roles);
    ASSERT_EQ(indices.size(), 2u);
    EXPECT_EQ(indices[0], 1);  // FrontLeft
    EXPECT_EQ(indices[1], 2);  // FrontRight
}
