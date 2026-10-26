#pragma once

namespace perlin::physics::bds {
    // LadderResetFallDamageSystem callback 0x10606B6C0. Standability is
    // membership in either native snow-capability component, not its payload.
    inline void ResetClimbingFallDistance(float& distance, bool wallClimbing, bool canClimb,
                                          bool climbableBlock, bool powderSnow, bool canStandOnSnow) {
        if (distance == 0.0f)
            return;
        if (wallClimbing || (canClimb && (climbableBlock || (powderSnow && canStandOnSnow))))
            distance = 0.0f;
    }

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
