#include "client/player/Bridging.hpp"
#include "client/player/BlockUse.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace perlin::bridging {
    namespace {
        constexpr double kEpsilon = 1.0e-5;
        constexpr double kJumpSupportDistance = 1.5;
        constexpr double kWorldLimit = 33554432.0;
        constexpr physics::Box kCube{0, 0, 0, 1, 1, 1};

        bool Finite(glm::dvec3 value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool Valid(const physics::Box& box) {
            return Finite({box.x0, box.y0, box.z0}) && Finite({box.x1, box.y1, box.z1}) && box.x0 < box.x1 &&
                   box.y0 < box.y1 && box.z0 < box.z1;
        }

        bool Intersects(const physics::Box& a, const physics::Box& b) {
            return a.x1 > b.x0 + kEpsilon && a.x0 < b.x1 - kEpsilon && a.y1 > b.y0 + kEpsilon &&
                   a.y0 < b.y1 - kEpsilon && a.z1 > b.z0 + kEpsilon && a.z0 < b.z1 - kEpsilon;
        }
    }

    bool OverlapsPlacement(const physics::Box& body, glm::ivec3 destination,
                           std::span<const physics::Box> placedShape) {
        const auto overlaps = [&](const physics::Box& shape) {
            return Valid(shape) &&
                   Intersects(body, shape.Offset(destination.x, destination.y, destination.z));
        };
        if (placedShape.empty())
            return overlaps(kCube);
        if (std::none_of(placedShape.begin(), placedShape.end(), Valid))
            return overlaps(kCube);
        return std::any_of(placedShape.begin(), placedShape.end(), overlaps);
    }

    std::optional<Target> FindTarget(const Request& request, const ReadCell& read) {
        if (!Valid(request.body) || !Finite(request.eye) || !Finite(request.direction) ||
            !std::isfinite(request.reach) || request.reach <= 0 || request.reach > 64 ||
            request.body.x1 - request.body.x0 > 4 || request.body.z1 - request.body.z0 > 4 ||
            std::abs(request.eye.x) > kWorldLimit || std::abs(request.eye.z) > kWorldLimit ||
            std::abs(request.body.x0) > kWorldLimit || std::abs(request.body.x1) > kWorldLimit ||
            std::abs(request.body.z0) > kWorldLimit || std::abs(request.body.z1) > kWorldLimit ||
            std::abs(request.body.y0) > kWorldLimit)
            return std::nullopt;
        const double length = glm::length(request.direction);
        if (!std::isfinite(length) || length < 1.0e-9)
            return std::nullopt;
        const glm::dvec3 direction = request.direction / length;
        if (direction.y >= -1.0e-6)
            return std::nullopt;

        std::map<std::array<int, 3>, Cell> cache;
        const auto cell = [&](glm::ivec3 position) -> const Cell& {
            const std::array key{position.x, position.y, position.z};
            const auto found = cache.find(key);
            if (found != cache.end())
                return found->second;
            return cache.emplace(key, read(position)).first->second;
        };

        const int minX = static_cast<int>(std::floor(request.body.x0 + kEpsilon));
        const int maxX = static_cast<int>(std::floor(request.body.x1 - kEpsilon));
        const int minZ = static_cast<int>(std::floor(request.body.z0 + kEpsilon));
        const int maxZ = static_cast<int>(std::floor(request.body.z1 - kEpsilon));
        const int maxY = static_cast<int>(std::floor(request.body.y0 + kEpsilon));
        const int minY = static_cast<int>(std::floor(request.body.y0 - kJumpSupportDistance)) - 1;
        std::optional<Target> best;
        double bestHeight = -std::numeric_limits<double>::infinity();
        for (int y = maxY; y >= minY; --y)
            for (int x = minX; x <= maxX; ++x)
                for (int z = minZ; z <= maxZ; ++z) {
                    const glm::ivec3 anchor{x, y, z};
                    const Cell& support = cell(anchor);
                    if (!support.loaded || support.replaceable || (support.interactive && !request.sneaking))
                        continue;
                    for (const auto& local : support.collision) {
                        if (!Valid(local))
                            continue;
                        const auto shape = local.Offset(x, y, z);
                        const double top = shape.y1;
                        if (top > request.body.y0 + kEpsilon ||
                            top < request.body.y0 - kJumpSupportDistance ||
                            shape.x1 <= request.body.x0 + kEpsilon ||
                            shape.x0 >= request.body.x1 - kEpsilon ||
                            shape.z1 <= request.body.z0 + kEpsilon || shape.z0 >= request.body.z1 - kEpsilon)
                            continue;
                        const double along = (top - request.eye.y) / direction.y;
                        if (along <= 0 || along > request.reach)
                            continue;
                        const glm::dvec3 projected = request.eye + direction * along;
                        if (!Finite(projected) || std::abs(projected.x) > kWorldLimit ||
                            std::abs(projected.z) > kWorldLimit)
                            continue;
                        const glm::ivec3 destination{static_cast<int>(std::floor(projected.x)), y,
                                                     static_cast<int>(std::floor(projected.z))};
                        const int dx = destination.x - x, dz = destination.z - z;
                        if (std::abs(dx) + std::abs(dz) != 1)
                            continue;
                        const Cell& target = cell(destination);
                        if (!target.loaded || !target.replaceable)
                            continue;

                        Target candidate;
                        candidate.anchor = anchor;
                        candidate.destination = destination;
                        candidate.face = dx < 0 ? 4 : dx > 0 ? 5 : dz < 0 ? 2 : 3;
                        const double clickLower = std::clamp(shape.y0, double(y), y + 1.0);
                        const double clickY = std::clamp(top - 0.001, clickLower, y + 1.0);
                        candidate.point = {std::clamp(projected.x, shape.x0, shape.x1), clickY,
                                           std::clamp(projected.z, shape.z0, shape.z1)};
                        if (dx)
                            candidate.point.x = dx < 0 ? shape.x0 : shape.x1;
                        else
                            candidate.point.z = dz < 0 ? shape.z0 : shape.z1;
                        if (request.placedShape.empty())
                            candidate.placedShape.push_back(kCube);
                        else
                            candidate.placedShape.assign(request.placedShape.begin(),
                                                         request.placedShape.end());
                        if (request.horizontalSlab && candidate.placedShape.size() == 1) {
                            const bool upper = blockuse::IsUpperSlabPlacement(
                                candidate.face, static_cast<float>(candidate.point.y - y));
                            candidate.placedShape[0].y0 = upper ? 0.5 : 0.0;
                            candidate.placedShape[0].y1 = upper ? 1.0 : 0.5;
                        }
                        if (OverlapsPlacement(request.body, destination, candidate.placedShape))
                            continue;
                        candidate.distance = glm::length(candidate.point - request.eye);
                        if (candidate.distance > request.reach)
                            continue;
                        if (!best || top > bestHeight + kEpsilon ||
                            (std::abs(top - bestHeight) <= kEpsilon && candidate.distance < best->distance)) {
                            best = candidate;
                            bestHeight = top;
                        }
                    }
                }
        return best;
    }

    bool PlacementRepeat::Ready(uint64_t now, glm::ivec3 destination, uint32_t item, bool pressed) const {
        if (pressed || !active || now < at)
            return true;
        if (now - firstAt < 200u)
            return false;
        const bool same = destination == lastDestination && item == lastItem;
        return now - at >= (same ? 200u : 50u);
    }

    void PlacementRepeat::Record(uint64_t now, glm::ivec3 destination, uint32_t item) {
        if (!active)
            firstAt = now;
        active = true;
        at = now;
        lastDestination = destination;
        lastItem = item;
    }
}
