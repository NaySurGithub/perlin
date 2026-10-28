#pragma once
#include "client/entity/EntityTracker.hpp"
#include "client/player/BlockCollision.hpp"

#include <functional>
#include <glm/glm.hpp>
#include <vector>

namespace perlin {
    struct AimHit {
        enum class Kind { None, Block, Entity } kind = Kind::None;
        int bx = 0, by = 0, bz = 0;
        int face = 1;
        glm::dvec3 point{0.0};
        double distance = 0;
        uint64_t entity = 0;
        glm::dvec3 boxLo{0.0}, boxHi{0.0};
        std::vector<std::pair<glm::dvec3, glm::dvec3>> shape;
    };

    glm::vec3 EntityClickOffset(const glm::dvec3& eye, const glm::dvec3& direction, const glm::dvec3& feet, double halfWidth,
                                double height, const glm::dvec3& fallback);

    AimHit Raycast(const Level& level, const BlockLibrary* blocks, const std::vector<entity::Entity>& entities,
                   const std::function<glm::vec2(const entity::Entity&)>& bounds, const glm::dvec3& eye,
                   const glm::dvec3& direction, double reach, double entityReach = -1.0);

    // GameMode::getPickRange (client 0x141f87030): the block-interaction range the game validates against.
    // Mouse 5.7, gamepad/motion 5.6, creative 12.0, VR 7.0 (12.0 creative).
    inline double PickRange(bool creative, int inputMode, bool vr = false) {
        if (vr)
            return creative ? 12.0 : 7.0;
        if (inputMode == 1)
            return 5.7;
        if (inputMode == 3 || inputMode == 4)
            return 5.6;
        if (creative)
            return 12.0;
        return 6.7;
    }
}
