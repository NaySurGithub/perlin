#include "client/player/HeldBuild.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::heldbuild {
    namespace {
        const glm::ivec3 kFaceNormal[6] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};

        int FaceOf(glm::ivec3 direction) {
            for (int f = 0; f < 6; f++)
                if (kFaceNormal[f] == direction)
                    return f;
            return -1;
        }

        bool UnitStep(glm::ivec3 d) { return std::abs(d.x) + std::abs(d.y) + std::abs(d.z) == 1; }
    }

    int MovementFace(glm::vec3 v) {
        if (!std::isfinite(v.x) || !std::isfinite(v.z))
            return -1;
        const float ax = std::fabs(v.x), az = std::fabs(v.z);
        if (std::max(ax, az) < 0.3f)  // blocks per second; below this the player is effectively still
            return -1;
        if (ax > az)
            return v.x > 0.0f ? 5 : 4;
        return v.z > 0.0f ? 3 : 2;
    }

    bool SegmentHitsCell(glm::vec3 a, glm::vec3 b, glm::ivec3 cell) {
        float enter = 0.0f, exit = 1.0f;
        for (int axis = 0; axis < 3; axis++) {
            const float lo = static_cast<float>(cell[axis]), hi = lo + 1.0f, d = b[axis] - a[axis];
            if (!std::isfinite(a[axis]) || !std::isfinite(d))
                return false;
            if (std::fabs(d) < 1e-9f) {
                if (a[axis] < lo || a[axis] > hi)
                    return false;
                continue;
            }
            const float t0 = (lo - a[axis]) / d, t1 = (hi - a[axis]) / d;
            enter = std::max(enter, std::min(t0, t1));
            exit = std::min(exit, std::max(t0, t1));
            if (enter > exit)
                return false;
        }
        return true;
    }

    bool OverlapsBody(glm::ivec3 cell, const std::vector<physics::Box>& shape, const physics::Box& body) {
        // A hair of tolerance so standing exactly on or against the new block is allowed.
        constexpr double kEpsilon = 1e-4;
        const physics::Box inner{body.x0 + kEpsilon, body.y0 + kEpsilon, body.z0 + kEpsilon,
                                 body.x1 - kEpsilon, body.y1 - kEpsilon, body.z1 - kEpsilon};
        for (const physics::Box& b : shape)
            if (b.Offset(cell.x, cell.y, cell.z).Intersects(inner))
                return true;
        return false;
    }

    std::optional<Target> HeldBuild::Choose(std::optional<Target> crosshair, glm::vec3 eye, glm::vec3 look, float reach,
                                            glm::vec3 velocity, bool sneaking) const {
        if (last && step && !sneaking && SegmentHitsCell(eye, eye + look * reach, *last + *step))
            return Target{*last, FaceOf(*step)};
        if (crosshair)
            return crosshair;
        if (!last || sneaking)
            return std::nullopt;
        const int face = MovementFace(velocity);
        if (face < 0)
            return std::nullopt;
        // Only build where the player could actually reach.
        const glm::vec3 centre = glm::vec3(*last + kFaceNormal[face]) + glm::vec3(0.5f);
        if (glm::length(centre - eye) > reach + 0.5f)
            return std::nullopt;
        return Target{*last, face};
    }

    void HeldBuild::Placed(glm::ivec3 cell) {
        if (last) {
            const glm::ivec3 d = cell - *last;
            if (UnitStep(d)) {
                if (pendingStep && *pendingStep == d)
                    step = d;
                else if (!step || *step != d)
                    step.reset();
                pendingStep = d;
            } else {
                step.reset();
                pendingStep.reset();
            }
        }
        last = cell;
    }
}
