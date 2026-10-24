#pragma once

namespace perlin::physics::bds {
    // CheckFallDamageSystem 0x1066C1ED0 passes MoveRequest's resolved Y
    // (+64) to Actor::_checkFallDamage 0x10A48CEEC. This is its scalar
    // FallDistanceComponent update; landing events/damage are separate.
    inline float FallDistanceAfterMove(float distance, float resolvedY, bool onGround,
                                       bool wasInWater, bool disabled = false, bool fallingBlock = false) {
        if (disabled)
            return 0.0f;
        if (wasInWater && !fallingBlock)
            distance = 0.0f;
        return !onGround && resolvedY < 0.0f ? distance - resolvedY : 0.0f;
    }
}
