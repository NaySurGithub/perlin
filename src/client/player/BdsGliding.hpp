#pragma once

#include "client/player/BdsRotation.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::physics::bds {
    // GlideMoveSystem callback 0x140C66F70. Multiplication barriers preserve
    // the reference's separate binary32 operations, including boost blends.
    inline Float3 GlideVelocity(Float3 velocity, Rotation previous, Rotation current, bool slowFalling,
                                bool boost, float& fallDistance) {
        const auto mul = [](float a, float b) { return detail::Round(a * b); };
        const auto add = [](float a, float b) { return detail::Round(a + b); };
        const auto sub = [](float a, float b) { return detail::Round(a - b); };
        if (velocity.y > -0.5f)
            fallDistance = 1.0f;
        const Float3 look = DirectionFromRotation(previous, current);
        const float originalX = velocity.x, originalZ = velocity.z;
        const float radians = mul(current.pitch, 0.017453292f);
        const float horizontalLook = std::sqrt(add(mul(look.x, look.x), mul(look.z, look.z)));
        const float horizontalSpeed = std::sqrt(add(mul(originalZ, originalZ), mul(originalX, originalX)));
        const float trigCoordinate = mul(radians, 10430.378f);
        const float cosine = SineTable()[SineIndex(add(trigCoordinate, 16384.0f))];
        const float lookLength =
            std::sqrt(add(add(mul(look.y, look.y), mul(look.x, look.x)), mul(look.z, look.z)));
        const float lift = mul(mul(std::min(mul(lookLength, 2.5f), 1.0f), cosine), cosine);
        const float gravity = slowFalling ? -0.01f : -0.08f;
        const float liftGravity = slowFalling ? -0.0075f : -0.06f;
        velocity.y = sub(velocity.y, sub(mul(liftGravity, lift), gravity));
        if (velocity.y < 0 && horizontalLook > 0) {
            const float descent = mul(mul(velocity.y, -0.1f), lift);
            const float reciprocal = detail::Round(1.0f / horizontalLook);
            velocity.x = add(mul(mul(descent, look.x), reciprocal), originalX);
            velocity.y = add(descent, velocity.y);
            velocity.z = add(mul(mul(descent, look.z), reciprocal), originalZ);
        }
        if (radians < 0) {
            const float climb = mul(mul(horizontalSpeed, SineTable()[SineIndex(trigCoordinate)]), -0.04f);
            const float nextY = add(mul(climb, 3.2f), velocity.y);
            const float reciprocal = detail::Round(1.0f / horizontalLook);
            velocity.x = sub(velocity.x, mul(mul(climb, look.x), reciprocal));
            velocity.y = nextY;
            velocity.z = sub(velocity.z, mul(mul(climb, look.z), reciprocal));
        }
        if (horizontalLook > 0) {
            const float reciprocal = detail::Round(1.0f / horizontalLook);
            const float nextX =
                add(mul(sub(mul(mul(reciprocal, look.x), horizontalSpeed), velocity.x), 0.1f), velocity.x);
            velocity.z =
                add(mul(sub(mul(mul(reciprocal, look.z), horizontalSpeed), velocity.z), 0.1f), velocity.z);
            velocity.x = nextX;
        }
        if (boost) {
            const auto boosted = [&](float speed, float direction) {
                return add(add(mul(sub(mul(direction, 1.5f), speed), 0.5f), mul(direction, 0.1f)), speed);
            };
            velocity.x = boosted(velocity.x, look.x);
            velocity.y = boosted(velocity.y, look.y);
            velocity.z = boosted(velocity.z, look.z);
        }
        return {mul(velocity.x, 0.99f), mul(velocity.y, 0.98f), mul(velocity.z, 0.99f)};
    }
}
