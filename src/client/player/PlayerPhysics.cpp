#include "client/player/PlayerPhysics.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace perlin::physics {
    namespace {
        constexpr double kEps = 1.0e-7;
        constexpr double kNegligible = 0.003;
        constexpr double kPi = 3.14159265358979323846;

        struct Vec3 {
            double x = 0, y = 0, z = 0;
        };

        double HorizontalSq(const Vec3& v) { return v.x * v.x + v.z * v.z; }

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

        // Java Entity.collideBoundingBox: Y first, then the larger horizontal axis.
        Vec3 CollideBox(const World& world, Box box, Vec3 move, std::vector<Box>& scratch) {
            if (move.x == 0 && move.y == 0 && move.z == 0)
                return move;
            Gather(world, box.Expand(move.x, move.y, move.z), scratch);
            Vec3 out = move;
            if (out.y != 0) {
                for (const Box& b : scratch)
                    out.y = b.ClipY(box, out.y);
                box = box.Offset(0, out.y, 0);
            }
            const bool zFirst = std::fabs(move.x) < std::fabs(move.z);
            for (int pass = 0; pass < 2; pass++) {
                const bool doZ = (pass == 0) == zFirst;
                if (doZ) {
                    if (out.z != 0) {
                        for (const Box& b : scratch)
                            out.z = b.ClipZ(box, out.z);
                        box = box.Offset(0, 0, out.z);
                    }
                } else if (out.x != 0) {
                    for (const Box& b : scratch)
                        out.x = b.ClipX(box, out.x);
                    box = box.Offset(out.x, 0, 0);
                }
            }
            return out;
        }

        bool NoCollision(const World& world, const Box& box, std::vector<Box>& scratch) {
            Gather(world, box, scratch);
            for (const Box& b : scratch)
                if (b.Intersects(box))
                    return false;
            return true;
        }

        // Java Entity.collide, including step-up.
        Vec3 Collide(const World& world, const Box& box, Vec3 move, bool onGround, double stepHeight,
                     std::vector<Box>& scratch) {
            Vec3 collided = CollideBox(world, box, move, scratch);
            const bool xCollision = move.x != collided.x;
            const bool yCollision = move.y != collided.y;
            const bool zCollision = move.z != collided.z;
            const bool landed = yCollision && move.y < 0.0;
            if (stepHeight > 0 && (onGround || landed) && (xCollision || zCollision)) {
                Vec3 up = CollideBox(world, box, {move.x, stepHeight, move.z}, scratch);
                const Vec3 lift = CollideBox(world, box.Expand(move.x, 0, move.z), {0, stepHeight, 0}, scratch);
                if (lift.y < stepHeight) {
                    Vec3 over = CollideBox(world, box.Offset(0, lift.y, 0), {move.x, 0, move.z}, scratch);
                    over.y += lift.y;
                    if (HorizontalSq(over) > HorizontalSq(up))
                        up = over;
                }
                if (HorizontalSq(up) > HorizontalSq(collided)) {
                    const Vec3 down = CollideBox(world, box.Offset(up.x, up.y, up.z), {0, -up.y + move.y, 0}, scratch);
                    return {up.x + down.x, up.y + down.y, up.z + down.z};
                }
            }
            return collided;
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
            bool water = false, lava = false, web = false, climbable = false;
        };

        // Scans the cells touching `box` (deflated like Java's fluid/inside-block checks).
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
                        env.climbable = env.climbable || s.climbable;
                    }
            return env;
        }

        void MoveRelative(State& s, double strafe, double forward, double scale) {
            double length = strafe * strafe + forward * forward;
            if (length < 1.0e-7)
                return;
            length = std::sqrt(length);
            if (length < 1.0)
                length = 1.0;
            const double k = scale / length;
            strafe *= k;
            forward *= k;
            const double yaw = s.yaw * kPi / 180.0;
            const double sn = std::sin(yaw), cs = std::cos(yaw);
            s.vx += -strafe * cs - forward * sn;
            s.vz += -strafe * sn + forward * cs;
        }

        // Java Entity.move: applies web slow-down, sneak edge protection and collisions.
        void MoveEntity(State& s, Vec3 move, bool sneaking, const World& world, std::vector<Box>& scratch,
                        const Environment& env) {
            if (env.web) {
                move.x *= 0.25;
                move.y *= 0.05;
                move.z *= 0.25;
                s.vx = s.vy = s.vz = 0;
            }

            const Box box = BoundingBox(s, sneaking);
            if (sneaking && s.onGround && s.vy <= 0.0) {
                auto free = [&](double dx, double dz) {
                    return NoCollision(world, box.Offset(dx, -kStepHeight, dz), scratch);
                };
                const double step = 0.05;
                auto shrink = [&](double v) { return (v < step && v >= -step) ? 0.0 : (v > 0 ? v - step : v + step); };
                while (move.x != 0 && free(move.x, 0))
                    move.x = shrink(move.x);
                while (move.z != 0 && free(0, move.z))
                    move.z = shrink(move.z);
                while (move.x != 0 && move.z != 0 && free(move.x, move.z)) {
                    move.x = shrink(move.x);
                    move.z = shrink(move.z);
                }
            }

            const Vec3 got = Collide(world, box, move, s.onGround, kStepHeight, scratch);
            s.x += got.x;
            s.y += got.y;
            s.z += got.z;

            const bool xBlocked = std::fabs(move.x - got.x) > 1.0e-9;
            const bool zBlocked = std::fabs(move.z - got.z) > 1.0e-9;
            const bool yBlocked = std::fabs(move.y - got.y) > 1.0e-9;
            s.horizontalCollision = xBlocked || zBlocked;
            s.verticalCollision = yBlocked;
            s.onGround = yBlocked && move.y < 0.0;
            if (xBlocked)
                s.vx = 0;
            if (zBlocked)
                s.vz = 0;
            if (yBlocked)
                s.vy = 0;
        }

        float SlipperinessBelow(const State& s, const World& world) {
            const int bx = static_cast<int>(std::floor(s.x));
            const int by = static_cast<int>(std::floor(s.y - 0.500001));
            const int bz = static_cast<int>(std::floor(s.z));
            return world.Query(bx, by, bz).friction;
        }

        double BlockSpeedFactor(const State& s, const World& world) {
            const int bx = static_cast<int>(std::floor(s.x));
            const int bz = static_cast<int>(std::floor(s.z));
            const BlockShape here = world.Query(bx, static_cast<int>(std::floor(s.y)), bz);
            if (here.fluid)
                return 1.0;
            if (here.speedFactor != 1.0f)
                return here.speedFactor;
            return world.Query(bx, static_cast<int>(std::floor(s.y - 0.500001)), bz).speedFactor;
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

        // Tiny velocities are zeroed first, as Java does.
        if (std::fabs(s.vx) < kNegligible)
            s.vx = 0;
        if (std::fabs(s.vy) < kNegligible)
            s.vy = 0;
        if (std::fabs(s.vz) < kNegligible)
            s.vz = 0;

        const Environment env = Scan(world, BoundingBox(s, sneaking));
        s.inWater = env.water;
        s.inLava = env.lava && !env.water;
        s.onClimbable = env.climbable;

        double forward = in.forward * 0.98;
        double strafe = in.strafe * 0.98;
        if (sneaking) {
            forward *= 0.3;
            strafe *= 0.3;
        }

        if (s.jumpDelay > 0)
            s.jumpDelay--;

        const bool fluid = (s.inWater || s.inLava) && !in.flying;
        if (in.flying) {
            // Creative flight: vertical input accelerates, vertical speed decays by 0.6.
            const double lift = attr.flySpeed * 3.0 * attr.verticalFlySpeed;
            if (in.flyUp)
                s.vy += lift;
            if (in.flyDown)
                s.vy -= lift;
            const double keep = s.vy;
            const double accel = s.onGround ? attr.speed * (0.16277136 / std::pow(SlipperinessBelow(s, world) * 0.91, 3.0))
                                            : attr.flySpeed * (in.sprint ? 2.0 : 1.0);
            MoveRelative(s, strafe, forward, accel);
            MoveEntity(s, {s.vx, s.vy, s.vz}, false, world, scratch, env);
            const double friction = s.onGround ? SlipperinessBelow(s, world) * 0.91 : 0.91;
            s.vx *= friction;
            s.vz *= friction;
            s.vy = keep * 0.6;
            if (s.verticalCollision)
                s.vy = 0;
            return;
        }

        if (in.jump) {
            if (fluid) {
                s.vy += 0.04;
            } else if (s.onGround && s.jumpDelay == 0) {
                s.vy = kJumpVelocity;
                if (in.sprint) {
                    const double yaw = s.yaw * kPi / 180.0;
                    s.vx -= std::sin(yaw) * kSprintJumpBoost;
                    s.vz += std::cos(yaw) * kSprintJumpBoost;
                }
                s.jumpDelay = 10;
            }
        } else {
            s.jumpDelay = 0;
        }

        if (fluid) {
            const double startY = s.y;
            if (s.inWater) {
                const double inertia = in.sprint ? 0.9 : 0.8;
                MoveRelative(s, strafe, forward, 0.02);
                MoveEntity(s, {s.vx, s.vy, s.vz}, false, world, scratch, env);
                if (s.onClimbable && s.horizontalCollision)
                    s.vy = 0.2;
                s.vx *= inertia;
                s.vy *= 0.8;
                s.vz *= inertia;
                if (!in.sprint)
                    s.vy -= kGravity / 16.0;
            } else {
                MoveRelative(s, strafe, forward, 0.02);
                MoveEntity(s, {s.vx, s.vy, s.vz}, false, world, scratch, env);
                s.vx *= 0.5;
                s.vy *= 0.5;
                s.vz *= 0.5;
                s.vy -= kGravity / 4.0;
            }
            // Hop out of the water when pushing against a ledge.
            if (s.horizontalCollision) {
                const Box probe = BoundingBox(s, false).Offset(s.vx, s.vy + 0.6 - s.y + startY, s.vz);
                if (NoCollision(world, probe, scratch) && !Scan(world, probe).water)
                    s.vy = 0.3;
            }
            return;
        }

        const double slip = s.onGround ? SlipperinessBelow(s, world) : 1.0;
        const double friction = s.onGround ? slip * 0.91 : 0.91;
        double accel;
        if (s.onGround) {
            const double speed = attr.speed * (in.sprint ? 1.3 : 1.0);
            accel = speed * (0.16277136 / (friction * friction * friction));
        } else {
            accel = in.sprint ? 0.026 : 0.02;
        }
        MoveRelative(s, strafe, forward, accel);

        if (s.onClimbable) {
            s.vx = std::clamp(s.vx, -0.15, 0.15);
            s.vz = std::clamp(s.vz, -0.15, 0.15);
            if (s.vy < -0.15)
                s.vy = -0.15;
            if (sneaking && s.vy < 0)
                s.vy = 0;
        }

        MoveEntity(s, {s.vx, s.vy, s.vz}, sneaking, world, scratch, env);

        if (s.onClimbable && (s.horizontalCollision || in.jump))
            s.vy = 0.2;

        const double factor = BlockSpeedFactor(s, world);
        s.vy = (s.vy - kGravity) * kAirDrag;
        s.vx *= friction * factor;
        s.vz *= friction * factor;
    }
}
