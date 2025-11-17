#include "audio_channel_layout.h"

#include <algorithm>
#include <numeric>

namespace screamrouter {
namespace audio {

namespace {

struct ChannelMaskEntry {
    uint32_t bit;
    ChannelRole role;
};

constexpr ChannelMaskEntry kChannelMaskLookup[] = {
    {0x00000001u, ChannelRole::FrontLeft},
    {0x00000002u, ChannelRole::FrontRight},
    {0x00000004u, ChannelRole::FrontCenter},
    {0x00000008u, ChannelRole::LowFrequency},
    {0x00000010u, ChannelRole::BackLeft},
    {0x00000020u, ChannelRole::BackRight},
    {0x00000040u, ChannelRole::FrontLeftOfCenter},
    {0x00000080u, ChannelRole::FrontRightOfCenter},
    {0x00000100u, ChannelRole::BackCenter},
    {0x00000200u, ChannelRole::SideLeft},
    {0x00000400u, ChannelRole::SideRight},
};

// Follows the Opus mapping family #1 canonical order (RFC 7845).
constexpr ChannelRole kPreferredFallbackOrder[] = {
    ChannelRole::FrontLeft,
    ChannelRole::FrontRight,
    ChannelRole::FrontCenter,
    ChannelRole::LowFrequency,
    ChannelRole::BackLeft,
    ChannelRole::BackRight,
    ChannelRole::SideLeft,
    ChannelRole::SideRight,
};

} // namespace

std::vector<ChannelRole> family1_canonical_channel_order(int channels) {
    switch (channels) {
        case 1:
            return {ChannelRole::FrontCenter};
        case 2:
            return {ChannelRole::FrontLeft, ChannelRole::FrontRight};
        case 3:
            return {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter};
        case 4:
            return {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::BackLeft, ChannelRole::BackRight};
        case 5:
            return {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                    ChannelRole::BackLeft, ChannelRole::BackRight};
        case 6:
            return {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                    ChannelRole::LowFrequency, ChannelRole::BackLeft, ChannelRole::BackRight};
        case 7:
            return {ChannelRole::FrontLeft, ChannelRole::FrontCenter, ChannelRole::FrontRight,
                    ChannelRole::BackLeft, ChannelRole::BackRight, ChannelRole::SideLeft, ChannelRole::SideRight};
        case 8:
            return {ChannelRole::FrontLeft, ChannelRole::FrontCenter, ChannelRole::FrontRight,
                    ChannelRole::SideLeft, ChannelRole::SideRight, ChannelRole::BackLeft,
                    ChannelRole::BackRight, ChannelRole::LowFrequency};
        default: {
            if (channels <= 0) {
                return {};
            }
            std::vector<ChannelRole> fallback;
            fallback.reserve(static_cast<size_t>(channels));
            for (int i = 0; i < channels; ++i) {
                fallback.push_back(kPreferredFallbackOrder[i % std::size(kPreferredFallbackOrder)]);
            }
            return fallback;
        }
    }
}

std::vector<ChannelRole> channel_order_from_mask(uint32_t mask) {
    std::vector<ChannelRole> order;
    for (const auto& entry : kChannelMaskLookup) {
        if (mask & entry.bit) {
            order.push_back(entry.role);
        }
    }
    return order;
}

uint32_t channel_mask_from_roles(const std::vector<ChannelRole>& roles) {
    uint32_t mask = 0;
    for (const auto& role : roles) {
        for (const auto& entry : kChannelMaskLookup) {
            if (entry.role == role) {
                mask |= entry.bit;
                break;
            }
        }
    }
    return mask;
}

uint32_t default_channel_mask_for_channels(int channels) {
    if (channels <= 0) {
        return 0x00000001u;
    }

    auto canonical = family1_canonical_channel_order(channels);
    if (canonical.empty()) {
        canonical.reserve(static_cast<size_t>(channels));
        for (int i = 0; i < channels; ++i) {
            canonical.push_back(kPreferredFallbackOrder[i % std::size(kPreferredFallbackOrder)]);
        }
    }

    return channel_mask_from_roles(canonical);
}

std::vector<int> roles_to_indices(const std::vector<ChannelRole>& roles) {
    std::vector<int> indices;
    indices.reserve(roles.size());
    for (ChannelRole role : roles) {
        indices.push_back(static_cast<int>(role));
    }
    return indices;
}

} // namespace audio
} // namespace screamrouter
