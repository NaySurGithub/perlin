#pragma once

#include "client/player/PerlinPhysicsNumerics.hpp"

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

    inline std::optional<Gravity> GroundGravity(float velocity, const GravityFlags& flags, bool slowFalling) {
        if (!flags.hasGravity ||
            ((flags.inScaffolding || flags.overScaffolding) && flags.descendThroughBlock))
            return std::nullopt;
        if (velocity < 0 && slowFalling)
            return Gravity{-0.01f, true};
        return Gravity{-0.08f, false};
    }

    inline std::optional<Gravity> PlayerWaterGravity(bool swimming) {
        if (swimming)
            return std::nullopt;
        return Gravity{-0.005f, false};
    }

    inline float ApplyGravity(float velocity, float& fallDistance, Gravity gravity) {
        if (gravity.resetFallDistance)
            fallDistance = 0;
        return gravity.acceleration + velocity;
    }

    inline float LevitateVelocity(float velocity, int level) {
        return detail::Round(velocity * 0.8f) + detail::Round(static_cast<float>(level) * 0.01f);
    }

    inline float GroundVerticalDrag(float velocity, bool hasGravity) {
        return hasGravity ? velocity * 0.98f : velocity;
    }

    inline float FlyingControl(float velocity, float speed, bool hover, bool creative,
                               bool up, bool down, bool upSlow = false, bool downSlow = false) {
        if (up && down)
            return 0.0f;
        if (hover && creative && !up && !down && !upSlow && !downSlow)
            velocity *= 0.375f;
        float acceleration = up ? 0.15f : 0.0f;
        if (upSlow) acceleration += 0.05f;
        if (down) acceleration -= 0.22f;
        if (downSlow) acceleration -= 0.15f;
        return detail::Round(acceleration * speed) + velocity;
    }

    inline float FlyingDragRetention(float amount) {
        const float upper = amount <= 1.0f ? amount : 1.0f;
        return amount >= 0.0f ? 1.0f - upper : 1.0f;
    }

    inline float FlyingHorizontalRetention(float coefficient, std::optional<float> override,
                                           std::optional<float> airDrag) {
        if (override)
            return (coefficient * *override) * 0.91f;
        const float modifier = airDrag.value_or(1.0f);
        const auto retention = [](float amount) {
            return amount < 0.0f ? 1.0f : 1.0f - (amount > 1.0f ? 1.0f : amount);
        };
        return retention((1.0f - coefficient) * modifier) * retention(0.089999974f * modifier);
    }

    inline float FlyingVerticalDrag(float velocity, float& fallDistance,
                                    std::optional<float> airDrag = std::nullopt) {
        fallDistance = 0;
        return velocity * FlyingDragRetention(airDrag ? *airDrag * 0.39999998f : 0.39999998f);
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

    inline float PostWaterVertical(float velocity, float& fallDistance, int levitationLevel, bool swimming) {
        if (levitationLevel > 0)
            return LevitateVelocity(velocity, levitationLevel);
        if (auto gravity = PlayerWaterGravity(swimming))
            velocity = ApplyGravity(velocity, fallDistance, *gravity);
        return velocity;
    }

    inline float BubbleColumnVelocity(float velocity, bool dragDown, float& fallDistance, bool surface = false) {
        fallDistance = 0;
        const float next = detail::Round(velocity + (dragDown ? -0.03f : surface ? 0.1f : 0.06f));
        if (dragDown)
            return next < (surface ? -0.9f : -0.3f) ? (surface ? -0.9f : -0.3f) : next;
        return next > (surface ? 1.8f : 0.7f) ? (surface ? 1.8f : 0.7f) : next;
    }
}
