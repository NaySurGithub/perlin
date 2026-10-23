#pragma once

#include "client/player/BdsCollision.hpp"
#include "client/player/BdsSupport.hpp"

#include <cstdint>
#include <optional>

namespace perlin::physics::bds {
    struct LiquidQueryBounds {
        CollisionBox water, lava, readiness;
    };

    // Finite-bounds path of LiquidBlocksFetch 0x140FC98E0. The optional
    // component that bypasses contraction is supplied by the caller; its
    // production is separate. A crossed axis collapses to its original center.
    inline LiquidQueryBounds MakeLiquidQueryBounds(const CollisionBox& box, bool bypassContraction = false) {
        LiquidQueryBounds result{box, box, box};
        if (bypassContraction) return result;
        const auto contract = [&](CollisionBox& output, float horizontal, float vertical) {
            for (int axis = 0; axis < 3; ++axis) {
                const float amount = axis == 1 ? vertical : horizontal;
                output[axis] = detail::Round(box[axis] + amount);
                output[axis + 3] = detail::Round(box[axis + 3] - amount);
                if (output[axis] > output[axis + 3]) {
                    const float middle = detail::Round(detail::Round(box[axis] + box[axis + 3]) * 0.5f);
                    output[axis] = output[axis + 3] = middle;
                }
            }
        };
        contract(result.lava, 0.1f, 0.4f);
        contract(result.water, 0.001f, 0.401f);
        for (int axis = 0; axis < 3; ++axis) {
            result.readiness[axis] = std::fmin(result.lava[axis], result.water[axis]);
            result.readiness[axis + 3] = std::fmax(result.water[axis + 3], result.lava[axis + 3]);
        }
        return result;
    }

    // LiquidBlocksFetch 0x140FC98E0 checks both materials and gives lava
    // priority when both contracted query regions contain fluid.
    inline uint32_t SelectFluidMaterial(bool water, bool lava) {
        return lava ? 6 : (water ? 5 : 0);
    }

    // Common single-body liquid collector, 0x140FC94C0 -> 0x140FC8FA0.
    // World-ready, finite, ordered bounds are supplied by the caller. Fluid
    // surface height is not consulted here; head sensing uses a separate path.
    // Retained records include boundary faces even when their cell origin is
    // outside the original upper bound. Material 25 is the native wildcard.
    template<class ReadMaterial, class Emit>
    bool CollectLiquidCells(const CollisionBox& bounds, uint32_t material, bool retain,
            ReadMaterial&& read, Emit&& emit) {
        BlockPosition begin, end;
        for (int axis = 0; axis < 3; ++axis) {
            begin[axis] = FloorBlockCoordinate(bounds[axis]);
            end[axis] = FloorBlockCoordinate(detail::Round(bounds[axis + 3] + 1.0f));
        }
        bool found = false;
        for (int32_t y = begin[1]; y < end[1]; ++y)
            for (int32_t z = begin[2]; z < end[2]; ++z)
                for (int32_t x = begin[0]; x < end[0]; ++x) {
                    const BlockPosition position{x, y, z};
                    const auto cellMaterial = read(position);
                    if (material != 25 && cellMaterial != material) continue;
                    if (retain) {
                        uint8_t faces = 0;
                        if (static_cast<float>(x) == static_cast<float>(begin[0])) faces |= 16;
                        if (static_cast<float>(x) == detail::Round(static_cast<float>(end[0]) - 1.0f)) faces |= 32;
                        if (static_cast<float>(z) == static_cast<float>(begin[2])) faces |= 4;
                        if (static_cast<float>(z) == detail::Round(static_cast<float>(end[2]) - 1.0f)) faces |= 8;
                        emit(position, faces);
                    }
                    if (!found && static_cast<float>(x) >= static_cast<float>(begin[0]) &&
                            static_cast<float>(x) <= bounds[3] &&
                            static_cast<float>(y) >= static_cast<float>(begin[1]) &&
                            static_cast<float>(y) <= bounds[4] &&
                            static_cast<float>(z) >= static_cast<float>(begin[2]) &&
                            static_cast<float>(z) <= bounds[5]) {
                        found = true;
                        if (!retain) return true;
                    }
                }
        return found;
    }

    // UpdateWaterStateRequestComponent::material, consumed by 0x140E0E6F0
    // and 0x140E0E820. This is the native material ID, not BlockShape::fluid.
    inline bool WasInLava(uint32_t material) { return material == 6; }

    // Complete water-state callback after component lookup. Boat membership
    // belongs to the passenger's referenced entity and registry generation.
    // Lookup remains lazy: non-water requests never query optional components.
    template<class IsBoatPassenger, class IsFallingBlock>
    void UpdateWaterState(uint32_t material, bool& wasInWater, float& fallDistance,
            std::optional<uint32_t>& splashRequest, IsBoatPassenger&& isBoatPassenger,
            IsFallingBlock&& isFallingBlock) {
        if (material != 5 || isBoatPassenger()) {
            wasInWater = false;
            return;
        }
        if (!isFallingBlock()) fallDistance = 0.0f;
        if (!wasInWater) {
            wasInWater = true;
            // Native get-or-create followed by an unconditional dword store.
            splashRequest = 1;
        }
    }
}
