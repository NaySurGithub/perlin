#pragma once

#include "client/player/PerlinPhysicsNumerics.hpp"
#include "client/player/PerlinInsideQuery.hpp"
#include <bit>

namespace perlin::physics::bds {
    // Native 26.52 ScaffoldingBlock collision provider 0x10B47CBAC.
    // The admitted shape is a full cube; the actor can enter from below/sides.
    inline bool ScaffoldingSupports(const CollisionBox& actor, const BlockPosition& block,
                                    int stability, bool fallThrough) {
        const auto next = [](int32_t value) {
            return std::bit_cast<int32_t>(static_cast<uint32_t>(value) + 1u);
        };
        return stability != 7 && !fallThrough &&
            actor[1] >= static_cast<float>(next(block[1])) - 0.000001f &&
            actor[3] > static_cast<float>(block[0]) && actor[0] < static_cast<float>(next(block[0])) &&
            actor[5] > static_cast<float>(block[2]) && actor[2] < static_cast<float>(next(block[2]));
    }
}
