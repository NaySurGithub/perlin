#pragma once

#include "client/player/PerlinCollision.hpp"

#include <cmath>
#include <limits>

namespace perlin::physics::bds {
    struct MovementFlags {
        bool horizontalCollision = false;
        bool verticalCollision = false;
        bool onGround = false;
        bool collision = false;
        bool collidableMobNear = false;
    };

    struct FinalizedMove {
        Float3 position;
        Float3 velocity;
        MovementFlags flags;
    };

    // FinalizeMoveSystem callback 0x140C638A0. Bounds/offset must produce a
    // valid BDS position. Producers and world-coordinate conversion are separate.
    inline FinalizedMove FinalizeMove(const CollisionBox& bounds, float verticalOffset,
                                      Float3 requested, Float3 resolved, Float3 velocity,
                                      MovementFlags flags, bool noClip, bool preserveHorizontalMotion) {
        const Float3 position{detail::Round(bounds[0] + bounds[3]) * 0.5f,
                              bounds[1] + verticalOffset,
                              detail::Round(bounds[2] + bounds[5]) * 0.5f};
        if (!noClip) {
            const float epsilon = std::numeric_limits<float>::epsilon();
            const float dx = std::fabs(requested.x - resolved.x);
            const float dy = std::fabs(requested.y - resolved.y);
            const float dz = std::fabs(requested.z - resolved.z);
            flags.horizontalCollision = dx > epsilon || dz > epsilon;
            flags.verticalCollision = !(dy <= epsilon);
            if (flags.verticalCollision && requested.y < 0)
                flags.onGround = true;
            else
                flags.onGround = flags.onGround && !(dy > epsilon) && requested.y == 0;
            flags.collision = flags.horizontalCollision || dy > epsilon;
        }
        if (!preserveHorizontalMotion) {
            if (resolved.x != requested.x)
                velocity.x = 0;
            if (resolved.z != requested.z)
                velocity.z = 0;
        }
        flags.collidableMobNear = false;
        return {position, velocity, flags};
    }
}
