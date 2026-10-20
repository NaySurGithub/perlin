#include "client/player/Interaction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace perlin {
    namespace {
        bool RayBox(const glm::dvec3& o, const glm::dvec3& d, const glm::dvec3& lo, const glm::dvec3& hi, double maxT, double& tOut, int& face) {
            double t0 = 0.0, t1 = maxT;
            int f = -1;
            for (int a = 0; a < 3; a++) {
                const double origin = o[a], dir = d[a], mn = lo[a], mx = hi[a];
                if (std::fabs(dir) < 1e-12) {
                    if (origin < mn || origin > mx)
                        return false;
                    continue;
                }
                double ta = (mn - origin) / dir, tb = (mx - origin) / dir;
                int fa = dir > 0 ? (a == 0 ? 4 : a == 1 ? 0 : 2) : (a == 0 ? 5 : a == 1 ? 1 : 3);
                if (ta > tb)
                    std::swap(ta, tb);
                if (ta > t0) {
                    t0 = ta;
                    f = fa;
                }
                t1 = std::min(t1, tb);
                if (t0 > t1)
                    return false;
            }
            tOut = t0;
            if (f < 0) {
                const glm::dvec3 ad = glm::abs(d);
                f = ad.y >= ad.x && ad.y >= ad.z ? (d.y > 0 ? 0 : 1) : ad.x >= ad.z ? (d.x > 0 ? 4 : 5) : (d.z > 0 ? 2 : 3);
            }
            face = f;
            return true;
        }
    }

    AimHit Raycast(const Level& level, const BlockLibrary* blocks, const std::vector<entity::Entity>& entities,
                   const std::function<glm::vec2(const entity::Entity&)>& bounds, const glm::dvec3& eye, const glm::dvec3& dirIn,
                   double reach, double entityReach) {
        AimHit best;
        best.distance = reach;
        const double len = glm::length(dirIn);
        if (len < 1e-9)
            return best;
        const glm::dvec3 dir = dirIn / len;

        const LevelCollision world(level, blocks);
        glm::ivec3 cell(static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.y)), static_cast<int>(std::floor(eye.z)));
        glm::ivec3 step(dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1);
        glm::dvec3 tMax, tDelta;
        for (int a = 0; a < 3; a++) {
            if (std::fabs(dir[a]) < 1e-12) {
                tMax[a] = std::numeric_limits<double>::infinity();
                tDelta[a] = std::numeric_limits<double>::infinity();
            } else {
                const double next = step[a] > 0 ? std::floor(eye[a]) + 1.0 : std::floor(eye[a]);
                tMax[a] = (next - eye[a]) / dir[a];
                tDelta[a] = std::fabs(1.0 / dir[a]);
            }
        }
        std::vector<physics::Box> selectionScratch;
        double traveled = 0;
        for (int i = 0; i < 64 && traveled <= reach; i++) {
            std::vector<physics::Box>& boxes = selectionScratch;
            const bool targetable = world.Selection(cell.x, cell.y, cell.z, boxes);
            if (targetable) {
                double bestT = std::numeric_limits<double>::infinity();
                int bestFace = 1;
                glm::dvec3 lo(0), hi(0);
                const auto test = [&](const glm::dvec3& l, const glm::dvec3& h) {
                    double t;
                    int f;
                    if (RayBox(eye, dir, l, h, reach, t, f) && t < bestT) {
                        bestT = t;
                        bestFace = f;
                        lo = l;
                        hi = h;
                    }
                };
                const glm::dvec3 base(cell);
                for (const physics::Box& x : boxes)
                    test(base + glm::dvec3(x.x0, x.y0, x.z0), base + glm::dvec3(x.x1, x.y1, x.z1));
                if (bestT <= reach) {
                    best.kind = AimHit::Kind::Block;
                    best.bx = cell.x; best.by = cell.y; best.bz = cell.z;
                    best.face = bestFace;
                    best.distance = bestT;
                    best.point = eye + dir * bestT;
                    best.boxLo = lo; best.boxHi = hi;
                    best.shape.clear();
                    for (const physics::Box& x : boxes)
                        best.shape.emplace_back(base + glm::dvec3(x.x0, x.y0, x.z0), base + glm::dvec3(x.x1, x.y1, x.z1));
                    break;
                }
            }
            int axis = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
            traveled = tMax[axis];
            cell[axis] += step[axis];
            tMax[axis] += tDelta[axis];
        }

        for (const entity::Entity& e : entities) {
            if (e.isSelf)
                continue;
            const glm::vec2 wh = bounds(e);
            const double hw = wh.x * 0.5 * (e.scale > 0 ? e.scale : 1.0f);
            const double pad = 0.1;
            const glm::dvec3 lo(e.pos.x - hw - pad, e.pos.y - pad, e.pos.z - hw - pad),
                hi(e.pos.x + hw + pad, e.pos.y + wh.y * (e.scale > 0 ? e.scale : 1.0f) + pad, e.pos.z + hw + pad);
            double t;
            int f;
            const double blockDist = best.kind == AimHit::Kind::Block ? best.distance + 0.75 : best.distance;
            const double cap = entityReach >= 0.0 ? entityReach : reach;
            const double limit = best.kind == AimHit::Kind::Entity ? std::min(best.distance, cap) : std::min(blockDist, cap);
            if (RayBox(eye, dir, lo, hi, limit, t, f) && t <= limit) {
                best.kind = AimHit::Kind::Entity;
                best.entity = e.runtimeId;
                best.distance = t;
                best.point = eye + dir * t;
                best.face = f;
                best.boxLo = lo + glm::dvec3(pad);
                best.boxHi = hi - glm::dvec3(pad);
                best.shape.clear();
                best.shape.emplace_back(best.boxLo, best.boxHi);
            }
        }
        return best;
    }
}
