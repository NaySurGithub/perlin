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
        glm::dvec3 point{}; // World-space click on the anchor's actual side face.
        double distance = 0;
        std::vector<physics::Box> placedShape;
    };

    struct Request {
        glm::dvec3 eye{}, direction{};
        physics::Box body;
        double reach = 5.7;
        bool sneaking = false;
        // Empty means a full cube. Otherwise use the held block's collision
        // boxes so lower slabs can extend a slab bridge below the player's feet.
        std::span<const physics::Box> placedShape;
        bool horizontalSlab = false;
    };

    using ReadCell = std::function<Cell(glm::ivec3)>;

    // Fallback for an unobstructed air ray while holding a placeable block.
    // A real block/entity hit keeps priority. Only a face-connected destination
    // adjoining an actual support under the player's footprint is eligible.
    std::optional<Target> FindTarget(const Request& request, const ReadCell& read);

    bool OverlapsPlacement(const physics::Box& body, glm::ivec3 destination,
                           std::span<const physics::Box> placedShape = {});

    // Fresh presses are immediate. Holding use can advance to a new destination
    // every game tick, while a stable/unconfirmed destination retries at 5 Hz.
    // The clock is monotonic milliseconds; Record also throttles failed attempts.
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
