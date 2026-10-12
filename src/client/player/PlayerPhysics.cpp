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

        void Gather(const World& world, const Box& region, std::vector<Box>& out) {
            out.clear();
            // Only shapes taller than their cell (fences, walls) reach upwards from the cell below.
            const int x0 = static_cast<int>(std::floor(region.x0 - kEps));
            const int y0 = static_cast<int>(std::floor(region.y0 - kEps)) - 1;
            const int z0 = static_cast<int>(std::floor(region.z0 - kEps));
            const int x1 = static_cast<int>(std::floor(region.x1 + kEps));
            const int y1 = static_cast<int>(std::floor(region.y1 + kEps));
            const int z1 = static_cast<int>(std::floor(region.z1 + kEps));
            const Box test = region.Inflate(kEps);
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                    for (int z = z0; z <= z1; z++) {
                        const BlockShape shape = world.Query(x, y, z);
                        for (int i = 0; i < shape.count; i++) {
                            const Box b = shape.boxes[i].Offset(x, y, z);
                            if (b.Intersects(test))
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

        double ClipAxis(const std::vector<Box>& colliders, const Box& cur, int axis, double v) {
            for (const Box& b : colliders)
                v = axis == 0 ? b.ClipX(cur, v) : (axis == 1 ? b.ClipY(cur, v) : b.ClipZ(cur, v));
            return v;
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
                double m = ClipAxis(colliders, cur, axis, v[axis]);
                if (axis != 1)
                    m = ClampTowardZero(m, v[axis]);
                cur = Translate(cur, axis, m);
                moved[axis] = m;
            }
            outBox = cur;
        }

        void ResolveStep(const Box& start, const Vec3& v, const std::vector<Box>& colliders, Box& outBox, Vec3& moved) {
            Box cur = start;
            const double up = ClipAxis(colliders, cur, 1, kStepHeight);
            cur = cur.Offset(0, up, 0);
            Vec3 horizontal;
            for (const int axis : {0, 2}) {
                const double m = ClipAxis(colliders, cur, axis, v[axis]);
                cur = Translate(cur, axis, m);
                horizontal[axis] = m;
            }
            const double down = ClipAxis(colliders, cur, 1, -up);
            cur = cur.Offset(0, down, 0);
            outBox = cur;
            moved = {horizontal.x, up + down, horizontal.z};
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

    bool Box::Intersects(const Box& o) const {
        return x0 < o.x1 - kEps && x1 > o.x0 + kEps && y0 < o.y1 - kEps && y1 > o.y0 + kEps && z0 < o.z1 - kEps &&
               z1 > o.z0 + kEps;
    }

    double Box::ClipX(const Box& m, double dx) const {
        if (m.y1 <= y0 + kEps || m.y0 >= y1 - kEps || m.z1 <= z0 + kEps || m.z0 >= z1 - kEps)
            return dx;
        if (dx > 0 && m.x1 <= x0 + kEps)
            return std::min(dx, x0 - m.x1);
        if (dx < 0 && m.x0 >= x1 - kEps)
            return std::max(dx, x1 - m.x0);
        return dx;
    }

    double Box::ClipY(const Box& m, double dy) const {
        if (m.x1 <= x0 + kEps || m.x0 >= x1 - kEps || m.z1 <= z0 + kEps || m.z0 >= z1 - kEps)
            return dy;
        if (dy > 0 && m.y1 <= y0 + kEps)
            return std::min(dy, y0 - m.y1);
        if (dy < 0 && m.y0 >= y1 - kEps)
            return std::max(dy, y1 - m.y0);
        return dy;
    }

    double Box::ClipZ(const Box& m, double dz) const {
        if (m.x1 <= x0 + kEps || m.x0 >= x1 - kEps || m.y1 <= y0 + kEps || m.y0 >= y1 - kEps)
            return dz;
        if (dz > 0 && m.z1 <= z0 + kEps)
            return std::min(dz, z0 - m.z1);
        if (dz < 0 && m.z0 >= z1 - kEps)
            return std::max(dz, z1 - m.z0);
        return dz;
    }

    Box BoundingBox(const State& s, bool sneaking) {
        const double h = sneaking ? kSneakHeight : kStandHeight;
        return {s.x - kWidth / 2, s.y, s.z - kWidth / 2, s.x + kWidth / 2, s.y + h, s.z + kWidth / 2};
    }

    void Step(State& s, const Input& in, const Attributes& attr, const World& world) {
        thread_local std::vector<Box> scratch;
        const bool sneaking = in.sneak && !in.flying;

        // Native simulation precision.
        s.x = F(s.x); s.y = F(s.y); s.z = F(s.z);
        s.vx = F(s.vx); s.vy = F(s.vy); s.vz = F(s.vz);
        if (s.vx * s.vx + s.vy * s.vy + s.vz * s.vz < 1.0e-12)
            s.vx = s.vy = s.vz = 0;

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
            s.onGround = m.stepped || (m.cy && req.y < 0.0) || (grounded && !m.cy && std::fabs(req.y) <= kFloatEps);
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
