#pragma once

#include "client/player/PerlinPhysicsNumerics.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace perlin::physics::bds {
    enum class LandingSurface { Ordinary, Bed, Slime };
    using BlockPosition = std::array<int32_t, 3>;

    inline int32_t FloorBlockCoordinate(float value) {
        // CVTTSS2SI, conditional floor correction, CVTTSS2SI as used by
        // the bed/slime handlers. Avoid an out-of-range C++ integer conversion.
        constexpr int32_t invalid = std::numeric_limits<int32_t>::min();
        if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
            return invalid;
        const int32_t truncated = static_cast<int32_t>(value);
        if (truncated == invalid)
            return invalid;
        return truncated - (static_cast<float>(truncated) != value && std::signbit(value) ? 1 : 0);
    }

    struct LandingResult {
        Float3 velocity;
        std::optional<BlockPosition> bouncePosition;
    };

    // VerticalCollisionSystem 0x140F1D820 dispatches to BlockLegacy slot 65.
    // Ordinary: 0x143F3BF60; bed: 0x143F0ECA0; slime: 0x144282EC0.
    // The bounce visitor/helper creates or updates ApplyRestitutionComponent.
    inline LandingResult VerticalCollision(Float3 velocity, Float3 position, LandingSurface surface,
                                            bool sneaking, bool minecart, bool hasBounceComponent,
                                            bool isPlayer, std::optional<Float3>& restitution) {
        LandingResult result{velocity, std::nullopt};
        if (surface != LandingSurface::Ordinary && !sneaking && velocity.y < 0) {
            if (!restitution)
                restitution = Float3{};
            const float coefficient = surface == LandingSurface::Bed ? 0.75f : 1.0f;
            restitution->y = -detail::Round(velocity.y * coefficient);
            if (minecart)
                restitution->y = detail::Round(restitution->y * 0.95f);
            if (hasBounceComponent && isPlayer)
                result.bouncePosition = BlockPosition{FloorBlockCoordinate(position.x),
                                                       FloorBlockCoordinate(position.y),
                                                       FloorBlockCoordinate(position.z)};
        }
        result.velocity.y = 0.0f;
        return result;
    }

    // ApplyRestitutionSystem callback 0x140C1BA90. Removal is a separate system.
    inline Float3 ApplyRestitution(Float3 velocity, Float3 restitution) {
        return {detail::Round(restitution.x + velocity.x), detail::Round(restitution.y + velocity.y),
                detail::Round(restitution.z + velocity.z)};
    }
}
