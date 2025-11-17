#pragma once

#include <cstdint>
#include <vector>

namespace screamrouter {
namespace audio {

/**
 * Channel identifiers roughly matching the canonical Opus ordering and
 * WAVEFORMATEXTENSIBLE channel definitions.
 */
enum class ChannelRole : int {
    FrontLeft = 1,
    FrontRight = 2,
    FrontCenter = 3,
    LowFrequency = 4,
    BackLeft = 5,
    BackRight = 6,
    FrontLeftOfCenter = 7,
    FrontRightOfCenter = 8,
    BackCenter = 9,
    SideLeft = 10,
    SideRight = 11,
};

/**
 * Returns the ordered list of channel roles specified by a dwChannelMask-style
 * bit field.
 */
std::vector<ChannelRole> channel_order_from_mask(uint32_t mask);

/**
 * Converts the ordered set of channel roles into a dwChannelMask bit field.
 */
uint32_t channel_mask_from_roles(const std::vector<ChannelRole>& roles);

/**
 * Builds the canonical Opus (mapping family 1) channel ordering for the given
 * channel count. Returns an empty vector if the count is unsupported.
 */
std::vector<ChannelRole> family1_canonical_channel_order(int channels);

/**
 * Provides a default channel mask for a stream that only advertises a channel
 * count. This is used when SAP data does not include an explicit layout.
 */
uint32_t default_channel_mask_for_channels(int channels);

/**
 * Utility to convert channel roles to their legacy integer identifiers.
 */
std::vector<int> roles_to_indices(const std::vector<ChannelRole>& roles);

} // namespace audio
} // namespace screamrouter
