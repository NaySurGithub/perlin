#include <array>
#include "client/player/PlayerPhysics.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// Bedrock player movement, ported tick-for-tick from the bedsim reference as implemented in cinnabar's
// `sim` crate: f32 velocity pipeline, Y->X->Z axis order, Bedrock step-up, ladder/water/flight rules.
namespace perlin::physics {
    namespace {
        constexpr double kEps = 1.0e-7;
        constexpr double kPi = 3.14159265358979323846;
        constexpr float kFloatEps = 1.1920929e-7f;
        constexpr double kClimbSpeed = 0.2;

        struct Vec3 {
            double x = 0, y = 0, z = 0;
            double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
            double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
        };

        double HorizontalSq(const Vec3& v) { return v.x * v.x + v.z * v.z; }
        double F(double v) { return static_cast<double>(static_cast<float>(v)); }

        // Every cell that can own a shape touching `region` (shapes reach one cell up from below: fences, walls),
        // filtered with the reference's strict f32 intersection so touching boxes are not colliders.
        void Gather(const World& world, const Box& region, std::vector<Box>& out) {
            out.clear();
            const int x0 = static_cast<int>(std::floor(region.x0)) - 1;
            const int y0 = static_cast<int>(std::floor(region.y0)) - 1;
            const int z0 = static_cast<int>(std::floor(region.z0)) - 1;
            const int x1 = static_cast<int>(std::floor(region.x1)) + 1;
            const int y1 = static_cast<int>(std::floor(region.y1)) + 1;
            const int z1 = static_cast<int>(std::floor(region.z1)) + 1;
            for (int x = x0; x <= x1; x++)
                for (int z = z0; z <= z1; z++)
                    for (int y = y0; y <= y1; y++) {
                        const BlockShape shape = world.Query(x, y, z);
                        for (int i = 0; i < shape.count; i++) {
                            const Box b = shape.boxes[i].Offset(x, y, z);
                            if (b.Intersects(region))
                                out.push_back(b);
                        }
                    }
        }

        bool NoCollision(const World& world, const Box& box, std::vector<Box>& scratch) {
            Gather(world, box, scratch);
            for (const Box& b : scratch)
                if (b.Intersects(box))
                    return false;
            return true;
        }

        bool ZeroVolume(const Box& b) { return b.x0 == b.x1 && b.y0 == b.y1 && b.z0 == b.z1; }

        double Lo(const Box& b, int a) { return a == 0 ? b.x0 : (a == 1 ? b.y0 : b.z0); }
        double Hi(const Box& b, int a) { return a == 0 ? b.x1 : (a == 1 ? b.y1 : b.z1); }

        // bedsim / Oomph swept-AABB clip of a moving box against one stationary box (cinnabar `Aabb::clip_against`),
        // evaluated in f32 with the 1e-7 contact snap so resting contact never counts as penetration.
        Vec3 ClipAgainst(const Box& self, const Box& stationary, Vec3 velocity) {
            if (ZeroVolume(stationary))
                return velocity;
            float axisPen[3] = {0, 0, 0}, signedPen[3] = {0, 0, 0}, normal[3] = {0, 0, 0};
            int separating = 0, separatingAxis = 0;
            for (int axis = 0; axis < 3; axis++) {
                float minPen = static_cast<float>(Hi(self, axis)) - static_cast<float>(Lo(stationary, axis));
                float maxPen = static_cast<float>(Hi(stationary, axis)) - static_cast<float>(Lo(self, axis));
                if (std::fabs(minPen) <= 1.0e-7f)
                    minPen = 0.0f;
                if (std::fabs(maxPen) <= 1.0e-7f)
                    maxPen = 0.0f;
                const float minPos = std::max(minPen, 0.0f), maxPos = std::max(maxPen, 0.0f);
                if (minPos == 0.0f) {
                    axisPen[axis] = 0.0f;
                    signedPen[axis] = minPen;
                    normal[axis] = -1.0f;
                    separating++;
                    separatingAxis = axis;
                } else if (maxPos == 0.0f) {
                    axisPen[axis] = 0.0f;
                    signedPen[axis] = maxPen;
                    normal[axis] = 1.0f;
                    separating++;
                    separatingAxis = axis;
                } else if (minPos < maxPos) {
                    axisPen[axis] = minPos;
                    signedPen[axis] = minPos;
                    normal[axis] = -1.0f;
                } else {
                    axisPen[axis] = maxPos;
                    signedPen[axis] = maxPos;
                    normal[axis] = 1.0f;
                }
                if (separating > 1)
                    return velocity;
            }
            if (separating == 0) {
                int best = 0;
                for (int axis = 1; axis < 3; axis++)
                    if (axisPen[axis] < axisPen[best])
                        best = axis;
                const float desired = axisPen[best] * normal[best];
                Vec3 out = velocity;
                const float v = static_cast<float>(velocity[best]);
                out[best] = static_cast<double>(desired > 0.0f ? std::max(desired, v) : std::min(desired, v));
                return out;
            }
            const float swept = signedPen[separatingAxis] - normal[separatingAxis] * static_cast<float>(velocity[separatingAxis]);
            if (swept <= 0.0f)
                return velocity;
            Vec3 out = velocity;
            out[separatingAxis] = static_cast<double>(signedPen[separatingAxis] * normal[separatingAxis]);
            return out;
        }

        Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
        Box TranslateBy(const Box& b, Vec3 d) { return b.Offset(d.x, d.y, d.z); }

        // Single-axis request, clipped against every collider in reference order (reverse for the plain move).
        double ClipAxis(const std::vector<Box>& colliders, const Box& cur, int axis, double v, bool reverse = false) {
            Vec3 vel;
            vel[axis] = v;
            if (reverse) {
                for (auto it = colliders.rbegin(); it != colliders.rend(); ++it)
                    vel = ClipAgainst(cur, *it, vel);
            } else {
                for (const Box& b : colliders)
                    vel = ClipAgainst(cur, b, vel);
            }
            return vel[axis];
        }

        Box Translate(const Box& b, int axis, double d) {
            return axis == 0 ? b.Offset(d, 0, 0) : (axis == 1 ? b.Offset(0, d, 0) : b.Offset(0, 0, d));
        }

        double ClampTowardZero(double value, double limit) {
            return limit >= 0 ? std::clamp(value, 0.0, limit) : std::clamp(value, limit, 0.0);
        }

        // Bedrock resolves Y, then X, then Z; horizontal axes can only be shortened.
        void ResolveAxes(const Box& start, const Vec3& v, const std::vector<Box>& colliders, Box& outBox, Vec3& moved) {
            Box cur = start;
            moved = {};
            for (const int axis : {1, 0, 2}) {
                double m = ClipAxis(colliders, cur, axis, v[axis], true);
                if (axis != 1)
                    m = ClampTowardZero(m, v[axis]);
                cur = Translate(cur, axis, m);
                moved[axis] = m;
            }
            outBox = cur;
        }

        void ResolveStep(const Box& start, const Vec3& v, const std::vector<Box>& colliders, Box& outBox, Vec3& moved) {
            Box cur = start;
            Vec3 up{0, kStepHeight, 0};
            for (const Box& b : colliders)
                up = ClipAgainst(cur, b, up);
            cur = TranslateBy(cur, up);
            Vec3 horizontal;
            for (const int axis : {0, 2}) {
                Vec3 vel;
                vel[axis] = v[axis];
                for (const Box& b : colliders)
                    vel = ClipAgainst(cur, b, vel);
                cur = TranslateBy(cur, vel);
                horizontal = Add(horizontal, vel);
            }
            Vec3 down{-up.x, -up.y, -up.z};
            for (const Box& b : colliders)
                down = ClipAgainst(cur, b, down);
            cur = TranslateBy(cur, down);
            outBox = cur;
            moved = Add(Add(horizontal, up), down);
        }

        struct Motion {
            Box box;
            Vec3 resolved;
            Vec3 position;
            bool cx = false, cy = false, cz = false;
            bool stepped = false;
        };

        Motion ResolveMotion(const World& world, const Box& start, const Vec3& v, bool wasOnGround,
                             std::vector<Box>& scratch) {
            Motion m;
            std::vector<Box>& colliders = scratch;
            Gather(world, start.Expand(v.x, v.y, v.z), colliders);
            Box normalBox;
            Vec3 normal;
            ResolveAxes(start, v, colliders, normalBox, normal);
            const bool normalHorizontal = normal.x != v.x || normal.z != v.z;
            const bool normalY = normal.y != v.y;
            const bool onGround = wasOnGround || (normalY && v.y < 0.0);
            Box box = normalBox;
            Vec3 resolved = normal;
            if (onGround && normalHorizontal) {
                std::vector<Box> envelope;
                Gather(world, start.Expand(v.x, kStepHeight, v.z), envelope);
                Box stepBox;
                Vec3 step;
                ResolveStep(start, v, envelope, stepBox, step);
                bool blocked = false;
                for (const Box& b : envelope)
                    if (b.Intersects(stepBox)) {
                        blocked = true;
                        break;
                    }
                if (!blocked && HorizontalSq(step) > HorizontalSq(normal)) {
                    box = stepBox;
                    resolved = step;
                    m.stepped = true;
                }
            }
            m.box = box;
            m.resolved = resolved;
            m.position = {F((static_cast<float>(box.x0) + static_cast<float>(box.x1)) * 0.5f), box.y0,
                          F((static_cast<float>(box.z0) + static_cast<float>(box.z1)) * 0.5f)};
            m.cx = std::fabs(static_cast<float>(v.x) - static_cast<float>(resolved.x)) > kFloatEps;
            m.cy = std::fabs(static_cast<float>(v.y) - static_cast<float>(resolved.y)) > kFloatEps;
            m.cz = std::fabs(static_cast<float>(v.z) - static_cast<float>(resolved.z)) > kFloatEps;
            return m;
        }

        double ReduceTowardZero(double value, double offset) {
            if (std::fabs(value) <= offset)
                return 0.0;
            return F(static_cast<float>(value) - (value < 0 ? -1.0f : 1.0f) * static_cast<float>(offset));
        }

        // Sneaking keeps the player on the edge: shortens the request until support stays within a step.
        Vec3 ClipSneakEdge(const World& world, const State& s, bool sneaking, Vec3 v, std::vector<Box>& scratch) {
            constexpr double kOffset = static_cast<double>(0.05f);
            const Box full = BoundingBox(s, sneaking);
            const Box player{F(static_cast<float>(full.x0) + 0.025f), full.y0, F(static_cast<float>(full.z0) + 0.025f),
                             F(static_cast<float>(full.x1) - 0.025f), full.y1, F(static_cast<float>(full.z1) - 0.025f)};
            const double drop = -kStepHeight * 1.01;
            Vec3 clipped = v;
            for (const int axis : {0, 2}) {
                while (clipped[axis] != 0.0) {
                    Vec3 probe{0, drop, 0};
                    probe[axis] = clipped[axis];
                    if (!NoCollision(world, player.Offset(probe.x, probe.y, probe.z), scratch))
                        break;
                    clipped[axis] = ReduceTowardZero(clipped[axis], kOffset);
                }
            }
            while (clipped.x != 0.0 || clipped.z != 0.0) {
                if (!NoCollision(world, player.Offset(clipped.x, drop, clipped.z), scratch))
                    break;
                clipped.x = ReduceTowardZero(clipped.x, kOffset);
                clipped.z = ReduceTowardZero(clipped.z, kOffset);
            }
            return clipped;
        }

        struct CellRange {
            int x0, y0, z0, x1, y1, z1;
        };

        CellRange Cells(const Box& b) {
            return {static_cast<int>(std::floor(b.x0)), static_cast<int>(std::floor(b.y0)),
                    static_cast<int>(std::floor(b.z0)), static_cast<int>(std::floor(b.x1)),
                    static_cast<int>(std::floor(b.y1)), static_cast<int>(std::floor(b.z1))};
        }

        struct Environment {
            bool water = false, lava = false, web = false;
        };

        // Scans the cells touching `box` (deflated like the vanilla fluid / inside-block checks).
        Environment Scan(const World& world, const Box& box) {
            Environment env;
            const Box shrunk = box.Inflate(-0.001);
            const CellRange c = Cells(shrunk);
            for (int y = c.y0; y <= c.y1; y++)
                for (int x = c.x0; x <= c.x1; x++)
                    for (int z = c.z0; z <= c.z1; z++) {
                        const BlockShape s = world.Query(x, y, z);
                        if (s.fluid && y + s.fluidLevel > shrunk.y0) {
                            if (s.fluid == 1)
                                env.water = true;
                            else
                                env.lava = true;
                        }
                        env.web = env.web || s.web;
                    }
            return env;
        }


        // Vanilla liquid current (cinnabar sim/world/flow.rs + current.rs). Returns false when unknown.
        bool SolidMotion(const BlockShape& s) { return !s.Empty() && !s.fluid && !s.climbable && !s.web; }

        struct Cell { int mat = 0; int depth = 0; };
        Cell LiquidCell(const World& w, int x, int y, int z, bool& unknown) {
            const BlockShape s = w.Query(x, y, z);
            if (!s.fluid) return {};
            if (s.fluidDepth == 255) { unknown = true; return {s.fluid, 0}; }
            return {s.fluid, s.fluidDepth};
        }
        inline int EffDepth(int d) { return d < 8 ? d : 0; }

        void FlowAt(const World& w, int x, int y, int z, int mat, float out[3]) {
            static const int dirAxis[4] = {2, 0, 2, 0};
            static const int dirStep[4] = {-1, 1, 1, -1};
            bool unk = false;
            const Cell c = LiquidCell(w, x, y, z, unk);
            const bool same = c.mat == mat;
            const int depth = same ? EffDepth(c.depth) : -1;
            float f[3] = {0, 0, 0};
            for (int d = 0; d < 4; d++) {
                int n[3] = {x, y, z};
                n[dirAxis[d]] += dirStep[d];
                const Cell o = LiquidCell(w, n[0], n[1], n[2], unk);
                if (o.mat == mat) {
                    f[dirAxis[d]] += static_cast<float>((EffDepth(o.depth) - depth) * dirStep[d]);
                    continue;
                }
                if (SolidMotion(w.Query(n[0], n[1], n[2])))
                    continue;
                const Cell below = LiquidCell(w, n[0], n[1] - 1, n[2], unk);
                if (below.mat == mat)
                    f[dirAxis[d]] += static_cast<float>((EffDepth(below.depth) - depth + 8) * dirStep[d]);
            }
            if (same && c.depth >= 8) {
                bool solid = false;
                for (int d = 0; d < 4 && !solid; d++) {
                    int n[3] = {x, y, z};
                    n[dirAxis[d]] += dirStep[d];
                    const BlockShape a = w.Query(n[0], n[1], n[2]);
                    const BlockShape b = w.Query(n[0], n[1] + 1, n[2]);
                    auto full = [](const BlockShape& s) {
                        return s.count == 1 && s.boxes[0].x0 <= 0 && s.boxes[0].z0 <= 0 && s.boxes[0].x1 >= 1 &&
                               s.boxes[0].z1 >= 1 && s.boxes[0].y0 <= 0 && s.boxes[0].y1 >= 1;
                    };
                    solid = full(a) || full(b);
                }
                if (solid) {
                    float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
                    if (l >= 0.0001f) { f[0] /= l; f[1] /= l; f[2] /= l; } else f[0] = f[1] = f[2] = 0;
                    f[1] += -6.0f;
                }
            }
            const float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
            if (l >= 0.0001f) { out[0] = f[0] / l; out[1] = f[1] / l; out[2] = f[2] / l; }
            else out[0] = out[1] = out[2] = 0;
        }

        // Adds the water/lava current at the start of the tick.
        void ApplyCurrent(State& s, const World& w, bool sneaking) {
            const float half = static_cast<float>(kWidth) * 0.5f;
            const float h = static_cast<float>(sneaking ? kSneakHeight : kStandHeight);
            const float px = static_cast<float>(s.x), py = static_cast<float>(s.y), pz = static_cast<float>(s.z);
            for (int mat = 2; mat >= 1; mat--) {
                const bool water = mat == 1;
                const float ix = water ? 0.001f : 0.1f, iy = water ? 0.401f : 0.4f;
                const float mnx = px - half, mxx = px + half, mny = py, mxy = py + h, mnz = pz - half, mxz = pz + half;
                const float cx = (mnx + mxx) * 0.5f, cy = (mny + mxy) * 0.5f, cz = (mnz + mxz) * 0.5f;
                const float lx = std::min(mnx + ix, cx), ly = std::min(mny + iy, cy), lz = std::min(mnz + ix, cz);
                const float hx = std::max(mxx - ix, cx), hy = std::max(mxy - iy, cy), hz = std::max(mxz - ix, cz);
                const int x0 = static_cast<int>(std::floor(lx)), y0 = static_cast<int>(std::floor(ly)),
                          z0 = static_cast<int>(std::floor(lz));
                const int x1 = static_cast<int>(std::floor(hx + 1.0f)), y1 = static_cast<int>(std::floor(hy + 1.0f)),
                          z1 = static_cast<int>(std::floor(hz + 1.0f));
                bool unknown = false, any = false;
                std::vector<std::array<int, 4>> cells;
                for (int y = y0; y < y1; y++)
                    for (int z = z0; z < z1; z++)
                        for (int x = x0; x < x1; x++) {
                            const Cell c = LiquidCell(w, x, y, z, unknown);
                            if (c.mat == mat) { any = true; cells.push_back({x, y, z, c.depth}); }
                        }
                if (!any) continue;
                if (unknown) return;
                bool admitted = false;
                for (auto& c : cells) if (c[3] > 0) { admitted = true; break; }
                if (!admitted) {
                    static const int ax[4] = {2, 0, 2, 0}, st[4] = {-1, 1, 1, -1};
                    for (auto& c : cells) {
                        for (int d = 0; d < 4 && !admitted; d++) {
                            const int edge = ax[d] == 0 ? (st[d] < 0 ? x0 : x1 - 1) : (st[d] < 0 ? z0 : z1 - 1);
                            if (c[ax[d] == 0 ? 0 : 2] != edge) continue;
                            int n[3] = {c[0], c[1], c[2]};
                            n[ax[d]] += st[d];
                            bool u = false;
                            const Cell o = LiquidCell(w, n[0], n[1], n[2], u);
                            if (o.mat == mat && !u && o.depth != 0) admitted = true;
                        }
                    }
                }
                if (!admitted) return;
                float sum[3] = {0, 0, 0};
                for (auto& c : cells) {
                    float f[3];
                    FlowAt(w, c[0], c[1], c[2], mat, f);
                    sum[0] += f[0]; sum[1] += f[1]; sum[2] += f[2];
                }
                const float l = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                if (l >= 0.0001f) {
                    const float k = water ? 0.014f : 0.0035f;
                    s.vx = F(static_cast<float>(s.vx) + sum[0] / l * k);
                    s.vy = F(static_cast<float>(s.vy) + sum[1] / l * k);
                    s.vz = F(static_cast<float>(s.vz) + sum[2] / l * k);
                }
                return; // lava takes precedence when both touch
            }
        }

        // Applies the f32 steering products the way the native client does.
        void MoveRelative(State& s, double strafeIn, double forwardIn, double speed) {
            const float strafe = static_cast<float>(strafeIn), forward = static_cast<float>(forwardIn);
            const float forceSq = forward * forward + strafe * strafe;
            if (forceSq < 1.0e-4f)
                return;
            const float force = static_cast<float>(speed) / std::max(std::sqrt(forceSq), 1.0f);
            const float yaw = static_cast<float>(s.yaw) * static_cast<float>(kPi / 180.0);
            const float sn = std::sin(yaw), cs = std::cos(yaw);
            // Bedrock strafe is left-positive; Perlin's input is right-positive.
            s.vx = F((-strafe * force * cs - sn * forward * force) + static_cast<float>(s.vx));
            s.vz = F((-strafe * force * sn + forward * force * cs) + static_cast<float>(s.vz));
        }

        double DampHorizontal(double value, float retention) {
            const float v = static_cast<float>(value);
            return std::fabs(v) <= kFloatEps ? 0.0 : static_cast<double>(v * retention);
        }

        const BlockShape& Below(const World& world, const State& s, BlockShape& storage) {
            // Friction probe: the cell 0.1 below the feet (air keeps the default).
            storage = world.Query(static_cast<int>(std::floor(s.x)), static_cast<int>(std::floor(s.y - 0.1f)),
                                  static_cast<int>(std::floor(s.z)));
            return storage;
        }
    }

    Box Box::Expand(double dx, double dy, double dz) const {
        Box b = *this;
        (dx < 0 ? b.x0 : b.x1) += dx;
        (dy < 0 ? b.y0 : b.y1) += dy;
        (dz < 0 ? b.z0 : b.z1) += dz;
        return b;
    }

    // Strict f32 comparison, as the reference's `Aabb::intersects`.
    bool Box::Intersects(const Box& o) const {
        return static_cast<float>(x1) > static_cast<float>(o.x0) && static_cast<float>(x0) < static_cast<float>(o.x1) &&
               static_cast<float>(y1) > static_cast<float>(o.y0) && static_cast<float>(y0) < static_cast<float>(o.y1) &&
               static_cast<float>(z1) > static_cast<float>(o.z0) && static_cast<float>(z0) < static_cast<float>(o.z1);
    }

    Box BoundingBox(const State& s, bool sneaking) {
        // Vanilla builds the box in f32 from the f32 position.
        const float half = static_cast<float>(kWidth) * 0.5f;
        const float x = static_cast<float>(s.x), y = static_cast<float>(s.y), z = static_cast<float>(s.z);
        const float h = static_cast<float>(sneaking ? kSneakHeight : kStandHeight);
        return {static_cast<double>(x - half), static_cast<double>(y), static_cast<double>(z - half),
                static_cast<double>(x + half), static_cast<double>(y + h), static_cast<double>(z + half)};
    }

    void Step(State& s, const Input& in, const Attributes& attr, const World& world) {
        thread_local std::vector<Box> scratch;
        const bool sneaking = in.sneak && !in.flying;

        // Native simulation precision.
        s.x = F(s.x); s.y = F(s.y); s.z = F(s.z);
        s.vx = F(s.vx); s.vy = F(s.vy); s.vz = F(s.vz);
        if (s.vx * s.vx + s.vy * s.vy + s.vz * s.vz < 1.0e-12)
            s.vx = s.vy = s.vz = 0;

        if (!in.flying)
            ApplyCurrent(s, world, sneaking);

        const bool prevHorizontal = s.horizontalCollision;
        const bool grounded = s.onGround;
        if (!in.jump)
            s.jumpDelay = 0;

        const Environment env = Scan(world, BoundingBox(s, sneaking));
        s.inWater = env.water;
        s.inLava = env.lava && !env.water;
        const BlockShape feet = world.Query(static_cast<int>(std::floor(s.x)), static_cast<int>(std::floor(s.y)),
                                            static_cast<int>(std::floor(s.z)));
        s.onClimbable = feet.climbable;

        BlockShape frictionStorage;
        const BlockShape& under = Below(world, s, frictionStorage);
        const float blockFriction = under.Empty() && !under.fluid && under.friction <= 0 ? 0.6f : under.friction;

        // Pose-adjusted input axes (sneak scales the request, the native 0.98 impulse follows).
        const float slow = sneaking ? 0.3f : 1.0f;
        const double forward = static_cast<float>(std::clamp<double>(in.forward, -slow, slow)) * 0.98f;
        const double strafe = static_cast<float>(std::clamp<double>(in.strafe, -slow, slow)) * 0.98f;

        // ---- spectator / ability flight -------------------------------------------------------------------
        if (in.flying) {
            const bool hovering = std::max(std::fabs(forward), std::fabs(strafe)) < 0.01f * 0.98f + 1e-9;
            const float hoverMod = hovering ? (in.creativeFlight ? 0.375f : 0.75f) : 1.0f;
            // Sneak descends instead of slowing the walk.
            const double fwd = static_cast<float>(in.forward) * 0.98f, str = static_cast<float>(in.strafe) * 0.98f;
            const bool hover = std::max(std::fabs(in.forward), std::fabs(in.strafe)) < 0.01f;
            (void)hoverMod;
            MoveRelative(s, str, fwd, static_cast<double>(attr.flySpeed) * (in.sprint ? 2.0 : 1.0));
            // vertical control
            if (in.flyUp && in.flyDown) {
                s.vy = 0;
            } else {
                float prev = static_cast<float>(s.vy);
                if (hover && in.creativeFlight && !in.flyUp && !in.flyDown)
                    prev *= 0.375f;
                const float accel = in.flyUp ? 0.15f : (in.flyDown ? -0.22f : 0.0f);
                s.vy = F(accel * attr.verticalFlySpeed + prev);
            }
            const float hMod = hover ? (in.creativeFlight ? 0.375f : 0.75f) : 1.0f;
            if (in.noClip) {
                s.x = F(s.x + s.vx);
                s.y = F(s.y + s.vy);
                s.z = F(s.z + s.vz);
                s.onGround = false;
                s.horizontalCollision = s.verticalCollision = false;
                s.jumpDelay = 0;
                const float h = 1.0f * hMod * 0.91f;
                s.vx = DampHorizontal(s.vx, h);
                s.vz = DampHorizontal(s.vz, h);
                s.vy = F(static_cast<float>(s.vy) * 0.6f);
                return;
            }
            const double groundFriction = grounded ? blockFriction : 1.0;
            Vec3 req{s.vx, s.vy, s.vz};
            if (env.web && !in.creativeFlight) {
                req.x *= 0.25; req.y *= 0.05; req.z *= 0.25;
            }
            const Box box = BoundingBox(s, false);
            const Motion m = ResolveMotion(world, box, req, grounded, scratch);
            s.x = m.position.x; s.y = m.position.y; s.z = m.position.z;
            // Only touching the floor counts; a hover must not keep the grounded flag from the take-off tick.
            s.onGround = m.cy && req.y < 0.0;
            s.vx = m.resolved.x; s.vy = m.resolved.y; s.vz = m.resolved.z;
            if (m.stepped || m.cy) s.vy = 0;
            if (m.cx) s.vx = 0;
            if (m.cz) s.vz = 0;
            s.horizontalCollision = m.cx || m.cz;
            s.verticalCollision = m.cy;
            const float h = static_cast<float>(groundFriction) * hMod * 0.91f;
            s.vx = DampHorizontal(s.vx, h);
            s.vz = DampHorizontal(s.vz, h);
            s.vy = F(static_cast<float>(s.vy) * 0.6f);
            if (s.onGround && s.verticalCollision && in.flyDown == false && m.cy && req.y < 0) {
                // landing ends flight in the caller
            }
            return;
        }

        // ---- walking, swimming-in-place, climbing ------------------------------------------------------------
        const bool water = s.inWater, lava = s.inLava;
        const float friction = grounded ? 0.91f * blockFriction : 0.91f;
        double relSpeed;
        if (water) {
            relSpeed = 0.02;
        } else if (lava) {
            relSpeed = 0.02;
        } else if (grounded) {
            const float speed = attr.speed * (in.sprint ? 1.3f : 1.0f);
            float accFriction = blockFriction;
            if (under.surface == 4)
                accFriction *= 1.225f;
            const float drag = accFriction * 0.91f;
            const float ratio = drag == 0.0f ? 1.0f : 0.54600006f / drag;
            relSpeed = speed * ratio * ratio * ratio;
        } else {
            relSpeed = in.sprint ? 0.026 : 0.02;
        }

        // A held jump in a liquid swims up before the move.
        if (in.jump && (water || lava))
            s.vy = F(static_cast<float>(s.vy) + 0.04f);

        MoveRelative(s, strafe, forward, relSpeed);

        const bool climbJump = in.jump && s.onClimbable;
        const bool jumpInitiated = in.jump && !climbJump && s.onGround && s.jumpDelay == 0 && !water && !lava;
        if (jumpInitiated) {
            s.vy = F(std::max(static_cast<float>(s.vy), 0.42f));
            s.jumpDelay = 10;
            if (in.sprint) {
                const float yaw = static_cast<float>(s.yaw) * static_cast<float>(kPi / 180.0);
                s.vx = F(static_cast<float>(s.vx) - std::sin(yaw) * 0.2f);
                s.vz = F(static_cast<float>(s.vz) + std::cos(yaw) * 0.2f);
            }
        }

        if (s.onClimbable) {
            s.vy = std::max(s.vy, -kClimbSpeed);
            if (climbJump || prevHorizontal)
                s.vy = kClimbSpeed;
            else if (sneaking && s.vy < 0)
                s.vy = 0;
        }
        if (water && sneaking)
            s.vy = F(static_cast<float>(s.vy) - 0.04f);

        Vec3 req{s.vx, s.vy, s.vz};
        const bool stuck = env.web;
        if (stuck) {
            req.x = F(static_cast<float>(req.x) * 0.25f);
            req.y = F(static_cast<float>(req.y) * 0.05f);
            req.z = F(static_cast<float>(req.z) * 0.25f);
        }
        Vec3 edgeVel = req;
        bool hasEdge = false;
        if (sneaking && grounded) {
            const Vec3 clipped = ClipSneakEdge(world, s, true, req, scratch);
            for (const int axis : {0, 2})
                if (std::fabs(static_cast<float>(clipped[axis])) <= kFloatEps)
                    edgeVel[axis] = 0;
            hasEdge = true;
            req = clipped;
        }
        const Vec3 pre = req;
        const Box box = BoundingBox(s, sneaking);
        const Motion m = ResolveMotion(world, box, req, grounded, scratch);
        s.x = m.position.x; s.y = m.position.y; s.z = m.position.z;
        s.onGround = m.cy ? (pre.y < 0.0) : (grounded && pre.y == 0.0);
        Vec3 v = m.resolved;
        if (hasEdge) {
            v.x = edgeVel.x;
            v.z = edgeVel.z;
        }
        if (m.stepped) v.y = 0;
        if (m.cx) v.x = 0;
        if (m.cy) {
            // Slime / bed rebound.
            const BlockShape support = world.Query(static_cast<int>(std::floor(s.x)),
                                                   static_cast<int>(std::floor(s.y - 0.2f)),
                                                   static_cast<int>(std::floor(s.z)));
            const bool bounces = !sneaking && static_cast<float>(pre.y) <= -0.08000012f;
            if (pre.y < 0 && support.surface == 1 && bounces)
                v.y = -pre.y;
            else if (pre.y < 0 && support.surface == 2 && bounces)
                v.y = F(-0.75f * static_cast<float>(pre.y));
            else
                v.y = 0;
        }
        if (m.cz) v.z = 0;
        if (stuck)
            v = {};
        s.horizontalCollision = m.cx || m.cz;
        s.verticalCollision = m.cy;

        const bool autoClimb =
            !water && !lava && (m.cx || m.cz) &&
            world.Query(static_cast<int>(std::floor(s.x)), static_cast<int>(std::floor(s.y)),
                        static_cast<int>(std::floor(s.z)))
                .climbable;
        if (autoClimb)
            v.y = kClimbSpeed;

        if (water || lava) {
            if (water) {
                const float base = in.sprint ? 0.9f : 0.8f;
                v.x = F(static_cast<float>(v.x) * base);
                v.y = F(static_cast<float>(v.y) * 0.8f);
                v.z = F(static_cast<float>(v.z) * base);
            } else {
                v.x = F(static_cast<float>(v.x) * 0.5f);
                v.y = F(static_cast<float>(v.y) * 0.5f);
                v.z = F(static_cast<float>(v.z) * 0.5f);
            }
            v.y = F(static_cast<float>(v.y) - (water ? 0.005f : 0.02f));
        } else {
            if (!autoClimb)
                v.y = F((static_cast<float>(v.y) - 0.08f) * 0.98f);
            v.x = DampHorizontal(v.x, friction);
            v.z = DampHorizontal(v.z, friction);
        }

        // Hop out of liquid when pushing against a ledge.
        if ((water || lava) && (m.cx || m.cz)) {
            const double startY = box.y0;
            const Box probe = BoundingBox(s, false).Offset(v.x, (startY - s.y) + 0.6 + v.y, v.z);
            if (NoCollision(world, probe, scratch) && !Scan(world, probe).water && !Scan(world, probe).lava)
                v.y = 0.3;
        }
        if (s.onGround && !sneaking && static_cast<float>(v.y) < 0.1f) {
            const BlockShape stand = world.Query(static_cast<int>(std::floor(s.x)),
                                                 static_cast<int>(std::floor(s.y - 0.5)),
                                                 static_cast<int>(std::floor(s.z)));
            if (stand.surface == 1 || stand.surface == 3) {
                const float factor = std::fabs(static_cast<float>(v.y)) * 0.2f + 0.4f;
                v.x = F(static_cast<float>(v.x) * factor);
                v.z = F(static_cast<float>(v.z) * factor);
            }
        }
        s.vx = v.x; s.vy = v.y; s.vz = v.z;
        if (s.jumpDelay > 0)
            s.jumpDelay--;
    }
}
