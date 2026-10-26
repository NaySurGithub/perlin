#pragma once

#include <cmath>
#include <limits>

namespace perlin::physics::bds {
    namespace detail {
        // BDS scalar SSE operations round separately to binary32. This barrier
        // also prevents contraction when a caller enables FMA.
        inline float Round(float value) {
            volatile float rounded = value;
            return rounded;
        }
    }

    struct Float3 {
        float x, y, z;
    };

    struct TravelIntentResult {
        Float3 movement;
        float turn;
        Float3 copiedMovement;
    };

    // Windows Server 1.26.10, MobTravelIntentSystem callback, 0x140EB2C20.
    inline TravelIntentResult AttenuateTravelIntent(Float3 movement, float turn) {
        movement.x *= 0.98f;
        movement.y *= 0.98f;
        movement.z *= 0.98f;
        turn *= 0.9f;
        return {movement, turn, movement};
    }

    // Shared friction kernel at 0x14103D8F0, reached from 0x14103D9D0.
    // Keep the multiplication even below the threshold: BDS preserves signed zero.
    inline float DampComponent(float velocity, float retention) {
        return velocity * (std::fabs(velocity) > std::numeric_limits<float>::epsilon() ? retention : 0.0f);
    }

    inline Float3 ApplyFriction(Float3 velocity, float coefficient, float multiplier, bool dampVertical,
                                bool vex) {
        const float retention = coefficient * multiplier;
        if (vex) {
            velocity.x = DampComponent(velocity.x, 0.91f);
            velocity.y = DampComponent(velocity.y, 0.91f);
            velocity.z = DampComponent(velocity.z, 0.91f);
        } else {
            velocity.x = DampComponent(velocity.x, retention);
            velocity.z = DampComponent(velocity.z, retention);
            if (dampVertical)
                velocity.y = DampComponent(velocity.y, retention);
        }
        return velocity;
    }

    // DecrementNoJumpDelaySystem callback, 0x140EB0DA0.
    inline int TickJumpDelay(int delay) {
        return delay > 0 ? delay - 1 : delay;
    }

    // GroundTravelTypeSystem kernels, 0x140E104A0 and 0x140E105A0.
    // The caller supplies the resolved friction after block/ability selection.
    inline float GroundAcceleration(float speed, float friction) {
        const float drag = friction * 0.91f;
        const float ratio = 0.54600006f / (drag == 0.0f ? 0.54600006f : drag);
        return ((ratio * ratio) * ratio) * speed;
    }
}
