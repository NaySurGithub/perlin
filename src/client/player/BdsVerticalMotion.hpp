#pragma once

#include "client/player/BdsPhysicsNumerics.hpp"

#include <optional>

namespace perlin::physics::bds {
    struct GravityFlags {
        bool hasGravity = true;
        bool inScaffolding = false;
        bool overScaffolding = false;
        bool descendThroughBlock = false;
    };

    struct Gravity {
        float acceleration;
        bool resetFallDistance = false;
    };

    // MobGroundGravity / MobAirGravity callback, 0x14103E8F0.
    inline std::optional<Gravity> GroundGravity(float velocity, const GravityFlags& flags, bool slowFalling) {
        if (!flags.hasGravity ||
            ((flags.inScaffolding || flags.overScaffolding) && flags.descendThroughBlock))
            return std::nullopt;
        if (velocity < 0 && slowFalling)
            return Gravity{-0.01f, true};
        return Gravity{-0.08f, false};
    }

    // PlayerWaterGravity callback 0x14104FC50 has no HAS_GRAVITY test.
    inline std::optional<Gravity> PlayerWaterGravity(bool swimming) {
        if (swimming)
            return std::nullopt;
        return Gravity{-0.005f, false};
    }

    // ApplyGravity callback 0x14104F970.
    inline float ApplyGravity(float velocity, float& fallDistance, Gravity gravity) {
        if (gravity.resetFallDistance)
            fallDistance = 0;
        return gravity.acceleration + velocity;
    }

    // LevitateSystem callback 0x14104F680. Level is amplifier + 1.
    inline float LevitateVelocity(float velocity, int level) {
        return detail::Round(velocity * 0.8f) + detail::Round(static_cast<float>(level) * 0.01f);
    }

    // AirVerticalDrag / GroundVerticalDrag callback 0x141051E00.
    inline float GroundVerticalDrag(float velocity, bool hasGravity) {
        return hasGravity ? velocity * 0.98f : velocity;
    }

    // PlayerFlyingDrag callback 0x141051C40.
    inline float FlyingVerticalDrag(float velocity, float& fallDistance) {
        fallDistance = 0;
        return velocity * 0.6f;
    }

    inline float PostGroundVertical(float velocity, float& fallDistance, const GravityFlags& flags,
                                    int levitationLevel, bool slowFalling, bool autoClimb,
                                    bool discardFriction = false) {
        if (levitationLevel > 0)
            velocity = LevitateVelocity(velocity, levitationLevel);
        else if (!autoClimb)
            if (auto gravity = GroundGravity(velocity, flags, slowFalling))
                velocity = ApplyGravity(velocity, fallDistance, *gravity);
        if (!autoClimb && !discardFriction)
            velocity = GroundVerticalDrag(velocity, flags.hasGravity);
        return velocity;
    }

    // Input velocity has already passed through WaterDragSystem.
    inline float PostWaterVertical(float velocity, float& fallDistance, int levitationLevel, bool swimming) {
        if (levitationLevel > 0)
            return LevitateVelocity(velocity, levitationLevel);
        if (auto gravity = PlayerWaterGravity(swimming))
            velocity = ApplyGravity(velocity, fallDistance, *gravity);
        return velocity;
    }
}
