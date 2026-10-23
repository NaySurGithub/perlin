#pragma once

#include "client/player/BdsInsideQuery.hpp"
#include "client/player/BdsSineTable.hpp"
#include "client/player/BdsFloat.hpp"

namespace perlin::physics::bds {
    inline constexpr uint64_t ReducedJumpSurfaceProperty = uint64_t{1} << 43;
    inline constexpr uint64_t ProbeBelowForJumpProperty = uint64_t{1} << 8;

    struct TriggerJumpRequest {
        bool reducedSurface = false;
        BlockPosition position{};
    };

    struct JumpParameters {
        // LavaSlimeJumpRequest selects the alternate callback; SquidJumpRequest
        // forces the low ordinary height. Native request production is separate.
        bool alternate = false;
        float bodyMinimumY = 0;
        Float3 position{};
        Float3 velocity{};
        Float3 previousJumpPosition{};
        float yaw = 0;
        bool sprinting = false;
        bool forceLowHeight = false;
        std::optional<float> customHeight;
        std::optional<int32_t> effectAmplifier;
        // Resolved type-2 integer in SynchedActorData slot 2.
        std::optional<int32_t> syncedJumpLevel;
        std::optional<TriggerJumpRequest> existingTrigger;
    };

    struct JumpResult {
        Float3 velocity;
        Float3 previousJumpPosition;
        TriggerJumpRequest trigger;
    };

    // Complete callbacks 0x140EAFAF0 and 0x140EAFFD0 after their ECS selection.
    // The query supplies BlockLegacy properties; optional providers are resolved
    // independently by the caller, including absence versus a supplied zero.
    template<class Query>
    JumpResult GroundJump(const JumpParameters& input, Query&& query) {
        BlockPosition position{FloorBlockCoordinate(input.position.x),
                               detail::FloorInsideCoordinate(input.bodyMinimumY),
                               FloorBlockCoordinate(input.position.z)};
        const auto properties = query(position);
        TriggerJumpRequest trigger;
        if (properties & ReducedJumpSurfaceProperty) {
            trigger = {true, position};
        } else if (static_cast<float>(position[1]) >= input.bodyMinimumY || (properties & ProbeBelowForJumpProperty)) {
            position[1] = std::bit_cast<int32_t>(static_cast<uint32_t>(position[1]) - 1);
            if (query(position) & ReducedJumpSurfaceProperty)
                trigger = {true, position};
        }
        const float multiplier = trigger.reducedSurface ? 0.6f : 1.0f;
        float height;
        if (input.alternate) {
            const float base = input.effectAmplifier
                ? detail::SseAdd(detail::SseMultiply(static_cast<float>(*input.effectAmplifier), 0.1f), 0.52f)
                : 0.42f;
            const float extra = detail::SseMultiply(static_cast<float>(input.syncedJumpLevel.value_or(0)), 0.1f);
            height = detail::SseMultiply(detail::SseAdd(extra, base), multiplier);
        } else {
            const float base = input.forceLowHeight ? 0.2f : input.customHeight.value_or(0.42f);
            const float effect = input.effectAmplifier
                ? detail::SseMultiply(detail::SseAdd(static_cast<float>(*input.effectAmplifier), 1.0f), 0.1f)
                : 0.0f;
            height = detail::SseMultiply(detail::SseAdd(effect, base), multiplier);
        }
        auto velocity = input.velocity;
        if (input.alternate || height > velocity.y)
            velocity.y = height;
        if (!input.alternate && input.sprinting)
            velocity = ApplySprintJump(velocity, input.yaw);
        return {velocity, input.alternate ? input.previousJumpPosition : input.position,
                input.existingTrigger.value_or(trigger)};
    }
}
