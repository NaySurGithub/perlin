#pragma once
#include "client/player/PlayerPhysics.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

// Placement while the use button is held, Bedrock style: after the first block, holding use keeps building from the
// block you just placed, so walking backwards (or sideways) off an edge extends a bridge without aiming at a face.
namespace perlin::heldbuild {
    // A support block and the face of it to click (0 down, 1 up, 2 north, 3 south, 4 west, 5 east).
    struct Target {
        glm::ivec3 support{};
        int face = 0;
    };

    class HeldBuild {
    public:
        // The target for this attempt, in priority order:
        //  1. Continuing a confirmed bridge: once two placements in a row stepped the same way, the next cell in that
        //     direction is used whenever the look ray (eye..eye+look*reach) passes through it.
        //  2. The crosshair's block face.
        //  3. The last placed block, on its side facing the player's horizontal movement (not while sneaking: sneaking
        //     builds only where you aim).
        std::optional<Target> Choose(std::optional<Target> crosshair, glm::vec3 eye, glm::vec3 look, float reach,
                                     glm::vec3 velocity, bool sneaking) const;
        // A placement was sent into `cell`.
        void Placed(glm::ivec3 cell);
        void Reset() { *this = HeldBuild{}; }

    private:
        std::optional<glm::ivec3> last;
        std::optional<glm::ivec3> step;  // confirmed by two consecutive unit steps in the same direction
        std::optional<glm::ivec3> pendingStep;
    };

    // Face index pointing along the dominant horizontal movement axis, or -1 when (nearly) standing still.
    int MovementFace(glm::vec3 velocity);

    // Whether a block with collision `shape` (block-local boxes) placed at `cell` would overlap `body`. Bedrock never
    // places a block into the player.
    bool OverlapsBody(glm::ivec3 cell, const std::vector<physics::Box>& shape, const physics::Box& body);

    // Whether the segment a..b passes through the unit cell (touching counts).
    bool SegmentHitsCell(glm::vec3 a, glm::vec3 b, glm::ivec3 cell);
}
