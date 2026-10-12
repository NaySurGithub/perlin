#pragma once
#include "client/player/PlayerPhysics.hpp"
#include "world/level/Level.hpp"

#include <cstdint>
#include <vector>

namespace perlin {
    class BlockLibrary;

    // Adapts the client's block world to the movement simulation: collision boxes, slipperiness,
    // fluids, ladders and cobwebs, derived from block names and states.
    class LevelCollision final : public physics::World {
    public:
        LevelCollision(const Level& level, const BlockLibrary* blocks) : level(level), blocks(blocks) {}

        physics::BlockShape Query(int x, int y, int z) const override;

    private:
        enum class Kind : uint8_t { Plain, Fence, Wall, Pane };
        struct Info {
            bool ready = false;
            Kind kind = Kind::Plain;
            bool full = false;
            physics::BlockShape shape;
        };

        const Info& Lookup(uint16_t id) const;
        Info Classify(uint16_t id) const;

        const Level& level;
        const BlockLibrary* blocks;
        mutable std::vector<Info> cache;
    };
}
