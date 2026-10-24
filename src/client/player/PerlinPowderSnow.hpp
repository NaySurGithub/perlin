#pragma once

namespace perlin::physics::bds {
    // PowderSnowBlock collision provider 0x10B464E74, player proxy branch.
    // Admission uses the full block top even for the reduced falling shape.
    inline float PowderSnowSupportHeight(float feetY, float blockTop, bool fallThrough,
                                         float fallDistance, bool canStand) {
        if (feetY < blockTop - 0.00000011920929f || fallThrough)
            return 0;
        if (!(fallDistance <= 2.5f))
            return 0.9f;
        return canStand ? 1.0f : 0.0f;
    }
}
