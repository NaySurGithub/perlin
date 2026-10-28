#pragma once

#include "client/player/PlayerPhysics.hpp"

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <vector>

namespace perlin::bridging {
    struct Cell {
        bool loaded = false;
        bool replaceable = false;
        bool interactive = false;
        std::vector<physics::Box> collision;
    };

    struct Target {
        glm::ivec3 anchor{}, destination{};
        int face = 1;
        glm::dvec3 point{};
        double distance = 0;
        std::vector<physics::Box> placedShape;
    };

    struct Request {
        glm::dvec3 eye{}, direction{};
        physics::Box body;
        double reach = 5.7;
        bool sneaking = false;
        std::span<const physics::Box> placedShape;
        bool horizontalSlab = false;
    };

    using ReadCell = std::function<Cell(glm::ivec3)>;

    std::optional<Target> FindTarget(const Request& request, const ReadCell& read);

    bool OverlapsPlacement(const physics::Box& body, glm::ivec3 destination,
                           std::span<const physics::Box> placedShape = {});

    class PlacementRepeat {
    public:
        bool Ready(uint64_t now, glm::ivec3 destination, uint32_t item, bool pressed) const;
        void Record(uint64_t now, glm::ivec3 destination, uint32_t item);
        void Reset() { active = false; }

    private:
        bool active = false;
        uint64_t at = 0;
        uint64_t firstAt = 0;
        glm::ivec3 lastDestination{};
        uint32_t lastItem = 0;
    };
}
