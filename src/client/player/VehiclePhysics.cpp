#include "client/player/VehiclePhysics.hpp"

#include <bit>
#include <cmath>

namespace perlin::physics::vehicle {
    namespace {
        float F32(uint32_t bits) { return std::bit_cast<float>(bits); }

        // SSE maxss/minss: the first operand only when strictly greater/less, else the second
        float MaxSs(float a, float b) { return a > b ? a : b; }
        float MinSs(float a, float b) { return a < b ? a : b; }

        struct Cell {
            int x, y, z;
        };

        Cell FloorCell(float x, float y, float z) {
            return {static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::floor(z))};
        }

        // liquid_blocks of the boat: water and flowing_water only
        bool Water(const BlockShape& s) { return s.fluid == 1; }

        // 0x143534d00: liquid top of the cell, from its liquid_depth state (missing state counts as 1)
        float LiquidTop(const BlockShape& s, int y) {
            const int n = s.fluidDepth < 8 ? s.fluidDepth + 1 : 1;
            return static_cast<float>(y + 1) - (static_cast<float>(n) / 9.0f + F32(0xbde38e39));
        }

        // 0x1437a1760: water at the position below its surface, no water above
        bool AtSurface(const World& world, float x, float y, float z) {
            const Cell c = FloorCell(x, y, z);
            const BlockShape here = world.Query(c.x, c.y, c.z);
            if (!Water(here) || LiquidTop(here, c.y) <= y)
                return false;
            return !Water(world.Query(c.x, c.y + 1, c.z));
        }

        // 0x1437a18f0: water here and above
        bool Submerged(const World& world, float x, float y, float z) {
            const Cell c = FloorCell(x, y, z);
            return Water(world.Query(c.x, c.y, c.z)) && Water(world.Query(c.x, c.y + 1, c.z));
        }

        // 0x1437d7990: the block's own friction when it has a collision box, the one below otherwise
        float FrictionAt(const World& world, const Cell& c) {
            const BlockShape here = world.CollisionQuery(c.x, c.y, c.z);
            if (!here.Empty())
                return here.friction;
            return world.Query(c.x, c.y - 1, c.z).friction;
        }

        // BoatMoveFrictionSystem 0x14ace0140
        void Friction(Boat& b, const World& world) {
            const Cell c = FloorCell(b.x, b.y, b.z);
            // TODO: leashed boats use BoatMovementComponent+16 instead of 1.0
            b.friction = 1.0f;
            b.uncontrolled = true;
            const float water = F32(0x3f666666);
            if (AtSurface(world, b.x, b.y, b.z)) {
                b.friction = water;
                b.uncontrolled = false;
                b.submergedTicks = 0;
            } else if (Submerged(world, b.x, b.y, b.z)) {
                b.friction = water;
                b.uncontrolled = false;
                ++b.submergedTicks;
            } else {
                const BlockShape here = world.Query(c.x, c.y, c.z);
                const bool air = here.isAir && here.fluid == 0;
                if (!air) {
                    if (Water(here)) {
                        b.friction = water;
                        b.uncontrolled = false;
                    } else if (b.onGround) {
                        b.friction = FrictionAt(world, c);
                        b.uncontrolled = false;
                    }
                } else if (b.onGround) {
                    b.friction = world.Query(c.x, c.y - 1, c.z).friction;
                    b.uncontrolled = false;
                } else if (Water(world.Query(c.x, c.y - 1, c.z))) {
                    b.friction = water;
                    b.uncontrolled = false;
                }
            }
            b.vx = b.friction * b.vx;
            b.vz = b.friction * b.vz;
            b.angularVelocity = b.friction * b.angularVelocity;
        }

        // VehicleBoatPaddleInputSystem 0x14b375070, analog lane: 3x the intent, eased for the first 10 ticks of a stroke
        void PaddleForce(BoatPaddles& p, int lane, float intent) {
            if (intent == 0.0f) {
                p.force[lane] = intent;
                return;
            }
            const float full = 3.0f * intent;
            const int32_t now = static_cast<int32_t>(p.tick);
            if (now - p.rowStart[lane] >= 10) {
                p.force[lane] = full;
                p.rowStart[lane] = now;
                return;
            }
            const float eased = MaxSs(0.0f, std::fabs(full) + F32(0xbdcccccd));
            p.force[lane] = 0.0f > intent ? -eased : eased;
        }

        // BoatMoveControlClientSystem 0x14ad15310: paddle thrust and torque, turn, then the friction factor again
        void Control(Boat& b, bool controlled) {
            if (b.submergedTicks > 24)
                return;
            float side = 0.0f, forward = 0.0f, torqueX = 0.0f, torqueY = 0.0f, torqueZ = 0.0f;
            if (controlled) {
                float strength = b.paddles.force[0] * F32(0x3c6147ae);
                if (strength == 0.0f) {
                    b.paddles.rowTime[0] = 0.0f;
                    b.paddles.rowStart[0] = -1;
                } else {
                    if (!b.uncontrolled) {
                        // (0, s, 0) crossed with the (3, 0, 0) lever, done lane-wise like the game
                        const float lane0 = 0.0f * strength;
                        torqueX = 0.0f + (0.0f * lane0 - lane0);
                        torqueY = 0.0f + (3.0f * strength - 0.0f * lane0);
                        torqueZ = (0.0f * lane0 - 3.0f * lane0) + 0.0f;
                        side = lane0 + 0.0f;
                        forward = strength;
                    }
                    const float time = strength + b.paddles.rowTime[0];
                    b.paddles.rowTime[0] = 1000.0f < time ? time + -1000.0f : time;
                }
                strength = b.paddles.force[1] * F32(0x3c6147ae);
                if (strength == 0.0f) {
                    b.paddles.rowTime[1] = 0.0f;
                    b.paddles.rowStart[1] = -1;
                } else {
                    if (!b.uncontrolled) {
                        const float lane0 = 0.0f * strength;
                        const float negativeZero = -0.0f * lane0;
                        const float lever = 3.0f * lane0 + negativeZero;
                        side = side + lane0;
                        torqueX = torqueX + (lane0 + negativeZero);
                        torqueY = torqueY + (-3.0f * strength - negativeZero);
                        torqueZ = torqueZ + lever;
                        forward = forward + strength;
                    }
                    const float time = strength + b.paddles.rowTime[1];
                    b.paddles.rowTime[1] = 1000.0f < time ? time + -1000.0f : time;
                }
            }
            float turn = torqueY;
            const float speedSquared = b.vz * b.vz + b.vx * b.vx;
            if (F32(0x3c23d70b) > speedSquared && (torqueX != 0.0f || torqueY != 0.0f || torqueZ != 0.0f)) {
                side = side * b.friction;
                forward = forward * b.friction;
                turn = turn * F32(0x3fcccccd);
            }
            turn = (turn * 10.0f + b.angularVelocity) * b.friction;
            b.angularVelocity = turn;
            b.yaw = turn + b.yaw;
            const float angle = (90.0f - b.yaw) * F32(0x3c8efa35);
            // CRT cosf/sinf imports, host libm is the nearest match
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const float dx = forward * sine + side * cosine;
            const float dz = forward * cosine - sine * side;
            const float vx = dx + b.vx;
            b.vy = side + b.vy;
            const float vz = dz + b.vz;
            b.vx = vx * b.friction;
            b.vz = vz * b.friction;
        }
    }

    Box Boat::Bounds() const {
        const float half = width * 0.5f;
        return {static_cast<double>(x - half), static_cast<double>(y), static_cast<double>(z - half),
                static_cast<double>(x + half), static_cast<double>(y + height), static_cast<double>(z + half)};
    }

    void PaddleIntent(float moveX, float moveZ, float& left, float& right) {
        const float z = 1.0f < moveZ ? 1.0f : MaxSs(moveZ, -1.0f);
        float length = std::sqrt(z * z + moveX * moveX);
        if (0.0f > z)
            length = length * F32(0xbe19999a);
        const float turn = z < 0.0f ? -moveX : moveX;
        const float rest = 1.0f - turn;
        float lane0 = 0.0f < turn ? rest + -0.0f : 1.0f;
        float lane1 = -1.0f < turn ? turn + 1.0f : 1.0f;
        if (1.0f < turn)
            lane0 = 0.0f;
        if (0.0f < turn)
            lane1 = 1.0f;
        left = length * lane0;
        right = length * lane1;
    }

    void TickBoat(Boat& b, const BoatInput& input, const Buoyancy& buoyancy, const World& world) {
        if (input.controlled) {
            float left = 0, right = 0;
            PaddleIntent(input.moveX, input.moveZ, left, right);
            PaddleForce(b.paddles, 0, left);
            PaddleForce(b.paddles, 1, right);
            ++b.paddles.tick;
        }
        Friction(b, world);
        Control(b, input.controlled);

        // BoatMoveRequestSystem 0x14ad19570: the move request is the posDelta
        const BoxMove moved = MoveBox(world, b.Bounds(), b.vx, b.vy, b.vz, b.onGround, b.recovery);
        b.x = moved.x;
        b.y = moved.y;
        b.z = moved.z;
        b.vx = moved.vx;
        b.vy = moved.vy;
        b.vz = moved.vz;
        b.onGround = moved.onGround;
        b.horizontalCollision = moved.horizontalCollision;
        b.verticalCollision = moved.verticalCollision;

        // IncreaseBuoyancyTimer 0x146959eb0; the big-wave roll uses the server's random stream, not modelled
        if (buoyancy.movementType == 2) {
            b.waveTimer = b.waveTimer + 1.0;
        } else {
            const double speed = static_cast<double>(std::sqrt(b.vz * b.vz + b.vx * b.vx)) * 30.0 + 1.0;
            b.waveTimer = static_cast<double>(static_cast<float>(speed * static_cast<double>(0.05f))) + b.waveTimer;
        }
        // CheckAndAddBuoyancyFloatRequest 0x146949c50
        b.atSurface = AtSurface(world, b.x, b.y, b.z);
        b.submerged = Submerged(world, b.x, b.y, b.z);
        // BuoyancyGravity 0x14695c240
        if (buoyancy.applyGravity)
            b.vy = (b.vy + F32(0xbd23d70a)) * F32(0x3f7ae148);
        // BuoyancyFloat 0x146959470
        float lift = 0.0f, wave = 0.0f;
        if (b.atSurface) {
            const float fraction = b.y - static_cast<float>(static_cast<int>(std::floor(b.y)));
            const float raw = (buoyancy.base - fraction) * F32(0x3f666666) + F32(0x3dcccccd);
            lift = 1.0f < raw ? 1.0f : MaxSs(raw, 0.0f);
            if (buoyancy.movementType == 1)
                wave = (static_cast<float>(std::sin(b.waveTimer)) + 1.0f) * F32(0x3d0f5c29);
            else if (buoyancy.movementType == 2)
                wave = (static_cast<float>(std::sin(b.waveTimer * static_cast<double>(F32(0x3ea0d97c)))) + 1.2f) * 0.5f;
        } else if (b.submerged) {
            lift = 1.0f;
        }
        if ((b.atSurface || b.submerged) && lift != 0.0f) {
            const float target = ((lift - wave) + F32(0xbdcccccd)) * F32(0x3e19999a);
            const float damped = b.vy * F32(0x3f333333);
            // TODO: leashed boats on newer base game versions take max(target, vy * 0.05) while rising
            b.vy = MinSs(target, damped + F32(0x3d4ccccd));
        }
    }
}
