#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace perlin::physics::bds {
    enum class MovementEffectType : int32_t { GlideBoost = 0, DolphinBoost = 1 };

    struct MovementEffect {
        uint64_t tick = 0;
        int32_t remaining = 0;
        int32_t type = -1;
    };

    using MovementEffects = std::array<MovementEffect, 2>;

    // 0x14387A590: an active longer/infinite effect rejects a shorter refresh.
    // This model accepts the two valid enum values in the audited BDS version.
    inline void ApplyMovementEffect(MovementEffects& effects, MovementEffect incoming) {
        if (incoming.type < 0 || incoming.type >= static_cast<int32_t>(effects.size()))
            return;
        auto& current = effects[incoming.type];
        if (incoming.remaining != current.remaining &&
            (current.remaining == -1 || (incoming.remaining != -1 && incoming.remaining < current.remaining)))
            return;
        current = incoming;
    }

    // MovementEffectsTick callback 0x1410114B0. Signed decrement wraps just
    // like the x86 instruction; zeroed expiry includes the source tick.
    inline void TickMovementEffects(MovementEffects& effects) {
        for (auto& effect : effects) {
            if (effect.type == -1 || effect.remaining == -1)
                continue;
            if (effect.remaining != 0)
                effect.remaining = std::bit_cast<int32_t>(std::bit_cast<uint32_t>(effect.remaining) - 1u);
            if (effect.remaining <= 0)
                effect = {};
        }
    }

    // DolphinBoostSystem::swimSpeedModifier callback 0x140B6FD10.
    inline float SwimSpeedMultiplier(bool swimming, const MovementEffects& effects) {
        return swimming && effects[static_cast<int32_t>(MovementEffectType::DolphinBoost)].type != -1 ? 2.0f
                                                                                                      : 1.0f;
    }
}
