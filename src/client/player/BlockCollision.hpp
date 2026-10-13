#pragma once
#include "client/player/PlayerPhysics.hpp"
#include "world/level/Level.hpp"

#include <cstdint>
#include <vector>

namespace perlin {
    class BlockLibrary;

    class LevelCollision final : public physics::World {
    public:
        LevelCollision(const Level& level, const BlockLibrary* blocks) : level(level), blocks(blocks) {}

        physics::BlockShape Query(int x, int y, int z) const override;

        static const physics::BlockShape& ShapeOf(uint16_t id);

    private:
        enum class Kind : uint8_t { Plain, Fence, Wall, Pane };
        struct Info {
            bool ready = false;
            Kind kind = Kind::Plain;
            bool full = false;
            physics::BlockShape shape;
        };

        physics::BlockShape QueryMain(int x, int y, int z) const;
        static const Info& Lookup(uint16_t id, const BlockLibrary* blocks);
        static Info Classify(uint16_t id, const BlockLibrary* blocks);

        const Level& level;
        const BlockLibrary* blocks;
    };
}
