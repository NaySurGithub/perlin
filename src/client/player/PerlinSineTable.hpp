#pragma once

#include "client/player/PerlinTrig.hpp"

#include <array>
#include <bit>
#include <cstdint>

namespace perlin::physics::bds {
    namespace detail {
        // BDS table initializer inputs stay within the vector sinf fast range.
        inline float TableEntry(uint32_t index) {
            const float angle = Round(static_cast<float>(index) * std::bit_cast<float>(0x38c90fdbu));
            return SineSmallPositive(angle);
        }
    }

    inline const std::array<float, 65536>& SineTable() {
        static const auto table = [] {
            std::array<float, 65536> result{};
            for (uint32_t index = 0; index < result.size(); ++index)
                result[index] = detail::TableEntry(index);
            return result;
        }();
        return table;
    }

    // CVTTSS2SI returns INT_MIN for invalid/out-of-range input; truncating that
    // to the BDS uint16 table coordinate selects entry zero.
    inline uint16_t SineIndex(float coordinate) {
        if (!(coordinate >= -2147483648.0f && coordinate < 2147483648.0f))
            return 0;
        return static_cast<uint16_t>(static_cast<int32_t>(coordinate));
    }

    // Ground-jump callback 0x140EAFAF0 converts yaw directly from degrees to
    // table coordinates. A radians conversion first can select another entry.
    inline uint16_t JumpSinIndex(float yaw) {
        return SineIndex(yaw * 182.04443f);
    }

    inline uint16_t JumpCosIndex(float yaw) {
        return SineIndex(detail::Round(yaw * 182.04443f) + 16384.0f);
    }

    inline Float3 ApplySprintJump(Float3 velocity, float yaw) {
        velocity.x = detail::Round(velocity.x - detail::Round(SineTable()[JumpSinIndex(yaw)] * 0.2f));
        velocity.z = detail::Round(velocity.z + detail::Round(SineTable()[JumpCosIndex(yaw)] * 0.2f));
        return velocity;
    }

    inline float CurrentSineEntry(uint16_t index) {
        static constexpr std::array<uint32_t, 65536> words = {
#include "PerlinCurrentSineTable.inc"
        };
        return std::bit_cast<float>(words[index]);
    }

    // 1.26.52.3 callback 0x14661F790 and integrated-server 0x1060774B8
    // round radians before the table conversion. The current table initializer
    // divides each index by 10430.378f and calls CRT sinf (0x14118BBF0).
    inline Float3 CurrentSprintJump(Float3 velocity, float yaw) {
        const float coordinate = detail::Round(detail::Round(yaw * 0.017453292f) * 10430.378f);
        velocity.x = detail::Round(velocity.x - detail::Round(CurrentSineEntry(SineIndex(coordinate)) * 0.2f));
        velocity.z = detail::Round(velocity.z + detail::Round(CurrentSineEntry(SineIndex(detail::Round(coordinate + 16384.0f))) * 0.2f));
        return velocity;
    }
}
