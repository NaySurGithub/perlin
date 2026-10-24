#pragma once

#include "client/player/PerlinFloat.hpp"

#include <cmath>
#include <limits>
#include <optional>

namespace perlin::physics::bds {
    // applySlowdownOnMove callback 0x140C1EF20. UCOMISS/JNZ treats
    // unordered lanes like zero here; even the smallest nonzero finite lane
    // activates the entire vector. Stored velocity is cleared before collision.
    inline bool ApplyBlockSlowdown(Float3& multiplier, Float3& movement, Float3& velocity) {
        bool active = false;
        for (float lane : {multiplier.x, multiplier.y, multiplier.z})
            active |= lane < 0.0f || lane > 0.0f;
        if (!active)
            return false;
        movement.x = detail::SseMultiply(multiplier.x, movement.x);
        movement.y = detail::SseMultiply(multiplier.y, movement.y);
        movement.z = detail::SseMultiply(multiplier.z, movement.z);
        velocity = {};
        multiplier = {};
        return true;
    }

    // EntityInside slowdown accumulator 0x140B58EA0 and first applied-record
    // constructor 0x140B80D80. Existing applied records retain their first value.
    inline void AccumulateBlockSlowdown(Float3& multiplier, const Float3& incoming,
                                        std::optional<bool>& firstApplication) {
        constexpr float epsilon = std::numeric_limits<float>::epsilon();
        const bool existing = std::fabs(multiplier.x) >= epsilon || std::fabs(multiplier.y) >= epsilon ||
                              std::fabs(multiplier.z) >= epsilon;
        if (!firstApplication)
            firstApplication = !existing;
        if (!existing) {
            multiplier = incoming;
        } else {
            if (multiplier.x > incoming.x)
                multiplier.x = incoming.x;
            if (multiplier.y > incoming.y)
                multiplier.y = incoming.y;
            if (multiplier.z > incoming.z)
                multiplier.z = incoming.z;
        }
    }

    // adjustFallDistance callback 0x140C1EF10; ECS membership supplies the byte.
    inline void AdjustSlowdownFallDistance(bool firstApplication, float& fallDistance) {
        if (firstApplication)
            fallDistance = 0.0f;
    }

    // immunePlayer 0x140C1EE80: MovementAbilities value bits 0 and 2 are
    // flying and instabuild. The native helper also erases the applied record.
    inline void ApplyPlayerSlowdownImmunity(Float3& multiplier, std::optional<bool>& firstApplication,
                                            bool flying, bool instabuild) {
        if (flying && instabuild) {
            multiplier = {};
            firstApplication.reset();
        }
    }
}
