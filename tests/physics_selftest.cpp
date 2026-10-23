#include "client/player/PlayerPhysics.hpp"

#include <cmath>
#include <bit>
#include <cstdio>
#include <functional>

using namespace perlin::physics;

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);          \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static bool Near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

struct TestWorld : World {
    std::function<BlockShape(int, int, int)> fn;
    std::function<bool(int, int)> ready;
    bds::TerrainHeightLimits limits;
    BlockShape Query(int x, int y, int z) const override { return fn(x, y, z); }
    bool CollisionChunkReady(int x, int z) const override { return ready ? ready(x, z) : true; }
    bds::TerrainHeightLimits CollisionHeightLimits() const override { return limits; }
};

static BlockShape Full() {
    BlockShape s;
    s.boxes.push_back({0, 0, 0, 1, 1, 1});
    return s;
}

static BlockShape Slab(double h) {
    BlockShape s;
    s.boxes.push_back({0, 0, 0, 1, h, 1});
    return s;
}

int main() {
    Attributes attr;

    // Honey contact is applied after motion and before accumulated slowdown.
    // The side contact resets fall distance and clamps downward velocity.
    {
        TestWorld honey;
        bool wet = false;
        bool secondContact = false;
        honey.fn = [&](int x, int y, int z) {
            BlockShape shape;
            shape.honey = (x == 0 || (secondContact && x == 1)) && y == 0 && z == 0;
            if (wet && shape.honey) {
                shape.fluid = 1;
                shape.fluidLevel = 1;
            }
            return shape;
        };
        Input input;
        input.gravity.hasGravity = false;
        State state;
        state.x = 1.25;
        state.y = 1;
        state.z = 0.5;
        state.vy = -0.5;
        state.fallDistance = 10;
        Step(state, input, attr, honey);
        CHECK(state.vy == static_cast<double>(-0.12f) && state.fallDistance == 0);
        state = {};
        wet = true;
        state.x = 1.25;
        state.y = 0.5;
        state.z = 0.5;
        state.vy = -0.5;
        Step(state, input, attr, honey);
        CHECK(state.inWater && state.vy < -0.12f);
        wet = false;
        state = {};
        state.x = state.z = 0.5;
        state.vx = 0.1f;
        Step(state, input, attr, honey);
        CHECK(Near(state.vx, 0.0364, 1e-8));
        secondContact = true;
        state = {};
        state.x = 0.95;
        state.z = 0.5;
        state.vx = 0.1f;
        Step(state, input, attr, honey);
        CHECK(Near(state.vx, 0.01456, 1e-8));
    }

    // Optional jump-height presence is distinct from a supplied zero. Surface
    // properties here are raw native masks, independent of block-name mapping.
    {
        TestWorld empty;
        empty.fn = [](int, int, int) { return BlockShape{}; };
        Input input;
        input.jump = true;
        input.gravity.hasGravity = false;
        State state;
        state.onGround = true;
        state.x = 2;
        state.z = -3;
        Attributes custom;
        custom.jumpHeight = 0;
        Step(state, input, custom, empty);
        CHECK(state.y == 0);
        CHECK(state.previousJumpPosition.x == 2 && state.previousJumpPosition.y == 0 &&
              state.previousJumpPosition.z == -3);
        state = {};
        state.onGround = true;
        custom.jumpHeight = 0.125f;
        Step(state, input, custom, empty);
        CHECK(state.y == 0.125f);
        empty.fn = [](int, int y, int) {
            BlockShape shape;
            if (y == -1) shape.jumpProperties = uint64_t{1} << 43;
            return shape;
        };
        state = {};
        state.onGround = true;
        Step(state, input, custom, empty);
        CHECK(state.y == static_cast<double>(0.125f * 0.6f));
        state = {};
        state.onGround = true;
        input.gravity.overScaffolding = true;
        input.gravity.descendThroughBlock = true;
        Step(state, input, custom, empty);
        CHECK(state.y == 0 && state.jumpDelay == 0);
        state = {};
        state.onGround = true;
        input.gravity.overScaffolding = false;
        Step(state, input, custom, empty);
        CHECK(state.y == static_cast<double>(0.125f * 0.6f));
    }

    // Slowdown activation is per-lane nonzero, including subnormals. It
    // clears StateVector velocity before collision response and is consumed.
    {
        TestWorld empty;
        empty.fn = [](int, int, int) { return BlockShape{}; };
        State state;
        state.vx = 1;
        state.stuckX = std::bit_cast<float>(uint32_t{1});
        Input input;
        input.gravity.hasGravity = false;
        Step(state, input, attr, empty);
        CHECK(state.vx == 0 && state.vy == 0 && state.vz == 0);
        CHECK(state.stuckX == 0 && state.stuckY == 0 && state.stuckZ == 0);
        state = {};
        state.stuckX = 0.25f;
        state.stuckY = 0.05f;
        state.stuckZ = 0.25f;
        state.restitution = bds::Float3{1, 2, 3};
        Step(state, input, attr, empty);
        CHECK(state.vx == static_cast<double>(0.91f));
        CHECK(state.vy == 2 && state.vz > 2.72 && state.vz < 2.74);

        TestWorld web;
        web.fn = [](int, int, int) {
            BlockShape shape;
            shape.web = true;
            return shape;
        };
        state = {};
        state.fallDistance = 10;
        Step(state, input, attr, web);
        CHECK(state.stuckX == 0.25f && state.stuckY == 0.05f && state.stuckZ == 0.25f);
        CHECK(state.fallDistance == 0);
        TestWorld supportedWeb;
        supportedWeb.fn = [](int, int y, int) {
            if (y < 0)
                return Full();
            BlockShape shape;
            shape.web = true;
            return shape;
        };
        state = {};
        state.onGround = true;
        state.vx = 1;
        state.stuckX = state.stuckZ = 0.25f;
        state.stuckY = 0.05f;
        input.sneak = true;
        Step(state, input, attr, supportedWeb);
        CHECK(state.x > 0.2 && state.vx == 0);
        input.sneak = false;
        // Contact processing also runs after flying movement, while flying
        // with instabuild removes the multiplier in immunePlayer.
        input.flying = true;
        state = {};
        Step(state, input, attr, web);
        CHECK(state.stuckX == 0.25f && state.stuckY == 0.05f && state.stuckZ == 0.25f);
        input.creativeFlight = true;
        state = {};
        Step(state, input, attr, web);
        CHECK(state.stuckX == 0 && state.stuckY == 0 && state.stuckZ == 0);
        web.ready = [](int, int) { return false; };
        state = {};
        input.flying = false;
        input.creativeFlight = false;
        Step(state, input, attr, web);
        CHECK(state.stuckX == 0 && state.stuckY == 0 && state.stuckZ == 0);
    }

    // Independent values from the executed BDS vertical-motion fixture. These
    // check that Step selects the verified callbacks, including metadata flags.
    {
        TestWorld empty;
        empty.fn = [](int, int, int) { return BlockShape{}; };
        State state;
        Attributes effects;
        effects.levitation = 1;
        Step(state, Input{}, effects, empty);
        CHECK(std::bit_cast<uint32_t>(static_cast<float>(state.vy)) == 0x3c20902e);
        state = {};
        Input input;
        input.gravity.hasGravity = false;
        Step(state, input, attr, empty);
        CHECK(state.vy == 0);
        state = {};
        input.gravity.hasGravity = true;
        input.gravity.inScaffolding = true;
        input.gravity.descendThroughBlock = true;
        Step(state, input, attr, empty);
        CHECK(state.vy == 0);
        state = {};
        state.vy = -0.42f;
        state.fallDistance = 11.25f;
        effects = {};
        effects.slowFalling = true;
        Step(state, Input{}, effects, empty);
        CHECK(state.fallDistance == 0);
        state = {};
        state.fallDistance = 11.25f;
        input = {};
        input.flying = true;
        Step(state, input, attr, empty);
        CHECK(state.fallDistance == 0);
    }

    // BDS caps a movement request to 16 blocks, retaining StateVector velocity
    // until collision response/friction. Clamping velocity itself loses motion.
    {
        TestWorld empty;
        empty.fn = [](int, int, int) { return BlockShape{}; };
        State state;
        state.vx = 100;
        Step(state, Input{}, attr, empty);
        CHECK(state.x == 16.0);
        CHECK(state.vx == 91.0);
        state = {};
        state.vx = 100;
        Input flying;
        flying.flying = true;
        Step(state, flying, attr, empty);
        CHECK(state.x == 16.0);
        CHECK(state.vx > 60.0);
    }

    // FinalizeMoveSystem preserves collision membership during no-clip and
    // consumes the nearby-mob tag. Its request flag preserves X/Z velocity
    // independently of collision membership and subsequent friction.
    {
        TestWorld wall;
        wall.fn = [](int x, int, int) { return x == 1 ? Full() : BlockShape{}; };
        State blocked;
        blocked.x = 0.5;
        blocked.vx = 1;
        blocked.collidableMobNear = true;
        State preserved = blocked;
        Input input;
        input.gravity.hasGravity = false;
        Step(blocked, input, attr, wall);
        // A nearby unavailable chunk contributes a preservation record even
        // though this particular impact is against an ordinary loaded block.
        wall.ready = [](int x, int) { return x >= 0; };
        Step(preserved, input, attr, wall);
        CHECK(blocked.vx == 0);
        CHECK(preserved.vx == static_cast<double>(0.91f));
        CHECK(blocked.x == preserved.x);
        CHECK(blocked.horizontalCollision && blocked.collision);
        CHECK(preserved.horizontalCollision && preserved.collision);
        CHECK(!blocked.collidableMobNear && !preserved.collidableMobNear);
        wall.ready = {};
        State flying = {};
        flying.onGround = true;
        flying.horizontalCollision = true;
        flying.verticalCollision = true;
        flying.collision = true;
        flying.collidableMobNear = true;
        input = {};
        input.flying = input.noClip = true;
        Step(flying, input, attr, wall);
        CHECK(flying.onGround && flying.horizontalCollision && flying.verticalCollision && flying.collision);
        CHECK(!flying.collidableMobNear);
        input.noClip = false;
        flying.horizontalCollision = flying.verticalCollision = flying.collision = false;
        Step(flying, input, attr, wall);
        CHECK(flying.onGround);
    }

    {
        TestWorld empty;
        empty.fn = [](int, int, int) { return BlockShape{}; };
        State state;
        state.fallDistance = 11.25f;
        Input gliding;
        gliding.gliding = true;
        gliding.movementEffects.push_back({42, 1, 0});
        Step(state, gliding, attr, empty);
        // Independent G fixture: level glide, initial rest, active boost.
        CHECK(std::bit_cast<uint32_t>(static_cast<float>(state.vx)) == 0x339e264e);
        CHECK(std::bit_cast<uint32_t>(static_cast<float>(state.vy)) == 0xbc1081c7);
        CHECK(std::bit_cast<uint32_t>(static_cast<float>(state.vz)) == 0x3f57a6ee);
        CHECK(state.fallDistance == 1);
        CHECK(state.movementEffects[0].type == -1);
        gliding.movementEffects.clear();
        gliding.pitch = -30;
        state.yaw = 45;
        Step(state, gliding, attr, empty);
        CHECK(state.previousRotation.pitch == -30 && state.previousRotation.yaw == 45);
    }

    // Native unavailable-chunk walls preserve motion and cover the complete
    // column, including heights outside Perlin's stored block range.
    {
        TestWorld loaded;
        loaded.fn = [](int, int, int) { return BlockShape{}; };
        loaded.ready = [](int x, int) { return x == 0; };
        State state;
        state.x = 15.5;
        state.z = 8;
        state.y = 500;
        state.vx = 1;
        Input input;
        input.gravity.hasGravity = false;
        Step(state, input, attr, loaded);
        CHECK(state.x < 16);
        CHECK(state.horizontalCollision);
        CHECK(state.vx == static_cast<double>(0.91f));
        loaded.ready = [](int, int) { return true; };
        Step(state, input, attr, loaded);
        CHECK(state.x > 16);
        CHECK(!state.horizontalCollision);
    }

    {
        TestWorld bounded;
        bounded.fn = [](int, int, int) { return BlockShape{}; };
        bounded.limits = {-64, 320};
        State state;
        state.x = state.z = 8;
        state.y = -103.9f;
        state.vy = -1;
        Input input;
        input.gravity.hasGravity = false;
        Step(state, input, attr, bounded);
        CHECK(state.y == -104);
        CHECK(state.onGround && state.verticalCollision);
        CHECK(state.vy == 0);
    }

    {
        TestWorld complex;
        complex.fn = [](int x, int y, int z) {
            BlockShape shape;
            if (x == 0 && y == 0 && z == 0) {
                shape.boxes.resize(31, Box{-10, 0, 0, -9, 1, 1});
                shape.boxes.push_back({0, 0, 0, 1, 1, 1});
            }
            return shape;
        };
        State state;
        state.x = 1.5;
        state.z = 0.5;
        state.vx = -1;
        Input input;
        input.gravity.hasGravity = false;
        Step(state, input, attr, complex);
        CHECK(state.horizontalCollision);
        CHECK(state.x > 1);
        CHECK(state.vx == 0);
    }

    {
        TestWorld shifted;
        shifted.fn = [](int x, int y, int z) {
            BlockShape shape;
            if (x == 1 && y == 0 && z == 0) {
                shape.boxes.push_back({0.5, 0, 0, 1, 1, 1});
                shape.randomOffset = bds::BlockRandomOffset{};
                shape.randomOffset->axes[0] = {-0.125f, -0.125f, 0};
            }
            return shape;
        };
        State state;
        state.x = -0.5;
        state.z = 0.5;
        state.vx = 1;
        Input input;
        input.gravity.hasGravity = false;
        Step(state, input, attr, shifted);
        CHECK(state.horizontalCollision);
        CHECK(state.x < 0.5);
        CHECK(state.vx == 0);
    }

    TestWorld flat;
    flat.fn = [](int, int y, int) { return y < 0 ? Full() : BlockShape{}; };

    // BDS block handlers bounce at speeds below the old 0.08 cutoff and
    // use StateVector velocity even when the move request was capped to 16.
    {
        TestWorld slime;
        slime.fn = [](int, int y, int) {
            auto block = y < 0 ? Full() : BlockShape{};
            block.surface = y < 0 ? 1 : 0;
            return block;
        };
        State state;
        state.vy = -0.01f;
        Input input;
        input.gravity.hasGravity = false;
        Step(state, input, attr, slime);
        CHECK(state.vy == static_cast<double>(0.01f));
        CHECK(state.onGround && state.verticalCollision);
        CHECK(!state.restitution);
        state = {};
        state.vy = -100;
        Step(state, input, attr, slime);
        CHECK(state.vy == 100);
        state = {};
        state.vy = -0.01f;
        input.sneak = true;
        Step(state, input, attr, slime);
        CHECK(state.vy == 0);
        state = {};
        state.vy = -0.01f;
        input.sneak = false;
        input.flying = true;
        Step(state, input, attr, slime);
        CHECK(state.vy > 0);
        state = {};
        state.y = 10;
        state.restitution = bds::Float3{0.125f, 0.5f, -0.25f};
        input.flying = false;
        Step(state, input, attr, slime);
        CHECK(state.y == 10);
        CHECK(state.vy == 0.5);
        CHECK(!state.restitution);

        // The player's center is over air, but the body lands on the edge of
        // the adjacent slime block. Select the block from cached geometry.
        slime.fn = [](int x, int y, int) {
            auto block = x == 0 && y == -1 ? Full() : BlockShape{};
            block.surface = 1;
            return block;
        };
        state = {};
        state.x = 1.1;
        state.z = 0.5;
        state.vy = -0.01f;
        Step(state, input, attr, slime);
        CHECK(state.onGround && state.verticalCollision);
        CHECK(state.vy == static_cast<double>(0.01f));
    }

    {
        const double starts[] = {3.3, 100.7, 1234.567, -987.654, 20000.1, -300000.3};
        const float yaws[] = {0, 33.3f, 90, 135.5f, 180, -90, -45.7f};
        for (double ox : starts)
            for (float yaw : yaws) {
                TestWorld wall;
                const int wx = static_cast<int>(std::floor(ox)) + 3, wz = static_cast<int>(std::floor(ox)) + 3;
                wall.fn = [&](int x, int y, int z) {
                    if (y < 0)
                        return Full();
                    return (x >= wx && y < 4) || (z >= wz && y < 4) ? Full() : BlockShape{};
                };
                State s;
                s.x = ox;
                s.z = ox;
                s.yaw = yaw;
                Input in;
                in.forward = 1;
                in.sprint = true;
                bool inside = false;
                for (int i = 0; i < 160; i++) {
                    if (i % 17 == 5)
                        in.jump = !in.jump;
                    Step(s, in, attr, wall);
                    const Box b = BoundingBox(s, false);
                    TestWorld& w = wall;
                    for (int x = wx - 1; x <= wx + 1; x++)
                        for (int z = wz - 1; z <= wz + 1; z++)
                            for (int y = 0; y < 4; y++) {
                                const BlockShape sh = w.Query(x, y, z);
                                for (int k = 0; k < sh.boxes.size(); k++)
                                    if (sh.boxes[k].Offset(x, y, z).Intersects(b))
                                        inside = true;
                            }
                }
                CHECK(!inside);
            }
    }

    {
        State s;
        s.y = 10;
        Input in;
        for (int i = 0; i < 80; i++)
            Step(s, in, attr, flat);
        CHECK(s.onGround);
        CHECK(Near(s.y, 0.0, 1e-6));
    }

    {
        State s;
        s.y = 200;
        Input in;
        for (int i = 0; i < 20; i++)
            Step(s, in, attr, flat);
        double vy = 0, y = 200;
        for (int i = 0; i < 20; i++) {
            y += vy;
            vy = (vy - 0.08) * 0.98;
        }
        CHECK(Near(s.y, y, 1e-3));
    }

    {
        State s;
        Input in;
        in.forward = 1;
        for (int i = 0; i < 60; i++)
            Step(s, in, attr, flat);
        const double z0 = s.z;
        for (int i = 0; i < 20; i++)
            Step(s, in, attr, flat);
        CHECK(Near(s.z - z0, 4.317, 0.02));
        State t;
        in.sprint = true;
        for (int i = 0; i < 60; i++)
            Step(t, in, attr, flat);
        const double t0 = t.z;
        for (int i = 0; i < 20; i++)
            Step(t, in, attr, flat);
        CHECK(Near(t.z - t0, 5.612, 0.03));
    }

    {
        State s;
        Input in;
        in.sneak = true;
        in.forward = ProcessMoveInput(1, 0, in.sneak, false).forward;
        for (int i = 0; i < 60; i++)
            Step(s, in, attr, flat);
        const double z0 = s.z;
        for (int i = 0; i < 20; i++)
            Step(s, in, attr, flat);
        CHECK(Near(s.z - z0, 4.317 * 0.3, 0.03));
    }

    {
        State s;
        s.yaw = 90;
        Input in;
        in.forward = 1;
        for (int i = 0; i < 10; i++)
            Step(s, in, attr, flat);
        CHECK(s.x < -0.5);
        CHECK(Near(s.z, 0.0, 1e-6));
        State r;
        Input rin;
        rin.strafe = 1;
        for (int i = 0; i < 10; i++)
            Step(r, rin, attr, flat);
        CHECK(r.x < -0.5);
    }

    {
        State s;
        s.onGround = true;
        Input in;
        in.jump = true;
        double top = 0;
        Step(s, in, attr, flat);
        in.jump = false;
        for (int i = 0; i < 30; i++) {
            top = std::max(top, s.y);
            Step(s, in, attr, flat);
        }
        CHECK(Near(top, 1.2522, 0.002));
    }

    {
        TestWorld wall;
        wall.fn = [](int x, int y, int z) {
            if (y < 0)
                return Full();
            if (z == 3 && y < 3)
                return Full();
            return BlockShape{};
        };
        State s;
        s.x = 0.5;
        s.z = 0.5;
        Input in;
        in.forward = 1;
        in.sprint = true;
        for (int i = 0; i < 60; i++)
            Step(s, in, attr, wall);
        CHECK(s.z <= 3.0 - 0.3 + 1e-6);
        CHECK(s.z > 2.6);
        CHECK(s.horizontalCollision);
    }

    {
        TestWorld steps;
        steps.fn = [](int, int y, int z) {
            if (y < 0)
                return Full();
            if (z == 3 && y == 0)
                return Slab(0.5);
            if (z == 6 && y == 0)
                return Full();
            return BlockShape{};
        };
        State s;
        s.x = 0.5;
        s.z = 0.5;
        Input in;
        in.forward = 1;
        for (int i = 0; i < 70; i++)
            Step(s, in, attr, steps);
        CHECK(s.z > 6.0 - 0.3 - 0.05 || s.z > 5.0);
        CHECK(s.z <= 6.0 - 0.3 + 1e-6);
    }

    {
        TestWorld edge;
        edge.fn = [](int, int y, int z) { return (y < 0 && z < 1) ? Full() : BlockShape{}; };
        State s;
        s.x = 0.5;
        s.z = 0.5;
        s.onGround = true;
        Input in;
        in.forward = 1;
        in.sneak = true;
        for (int i = 0; i < 60; i++)
            Step(s, in, attr, edge);
        CHECK(Near(s.y, 0.0, 1e-6));
        CHECK(s.z < 1.31);
        // ActorDataFlag 6 (TEMPTED) bypasses the native sneak edge filter.
        s = {};
        s.x = s.z = 0.5;
        s.onGround = true;
        in.actorMovementFlags = 0x40;
        for (int i = 0; i < 60; ++i)
            Step(s, in, attr, edge);
        CHECK(s.z > 1.31 && s.y < 0);
    }

    {
        TestWorld water;
        water.fn = [](int, int y, int) {
            BlockShape s;
            if (y < -5)
                return Full();
            s.fluid = 1;
            s.fluidLevel = 0.89f;
            return s;
        };
        State s;
        s.y = 0;
        Input in;
        for (int i = 0; i < 40; i++)
            Step(s, in, attr, water);
        CHECK(s.inWater);
        CHECK(s.vy > -0.1);
    }

    {
        TestWorld water;
        water.fn = [](int, int, int) {
            BlockShape shape;
            shape.fluid = 1;
            shape.fluidLevel = 1;
            shape.fluidDepth = 0;
            return shape;
        };
        State state;
        state.vx = 0.42f;
        state.vy = -0.08f;
        state.vz = 10;
        Attributes enchanted;
        enchanted.waterWalkSpeedEnchant = 3;
        Step(state, Input{}, enchanted, water);
        CHECK(state.inWater);
        // Airborne enchantment drag: independent native W fixture result.
        CHECK(std::bit_cast<uint32_t>(static_cast<float>(state.vx)) == 0x3e90b8d0);
        state = {};
        Input swimming;
        swimming.swimming = true;
        Step(state, swimming, attr, water);
        CHECK(state.vy == 0);

        // Timed dolphin boosts enter through recorded input and persist in the
        // state used for prediction replay. A one-tick boost affects this move
        // before the post-movement expiry pass.
        state = {};
        swimming.forward = 1;
        swimming.movementEffects.push_back({42, 1, 1});
        const State beforeBoost = state;
        Step(state, swimming, attr, water);
        CHECK(state.movementEffects[1].type == -1);
        CHECK(state.z > 0.027 && state.z < 0.028);
        State replay = beforeBoost;
        Step(replay, swimming, attr, water);
        CHECK(replay.z == state.z && replay.vz == state.vz);
        CHECK(replay.movementEffects[1].type == state.movementEffects[1].type);

        // BDS accepts a zero underwater attribute; it must not be replaced by
        // a default speed, and an absent optional attribute also selects zero.
        Attributes stopped;
        stopped.underwaterSpeed = 0;
        state = {};
        swimming.movementEffects.clear();
        Step(state, swimming, stopped, water);
        CHECK(state.z == 0 && state.vz == 0);
        stopped.underwaterSpeed.reset();
        state = {};
        Step(state, swimming, stopped, water);
        CHECK(state.z == 0 && state.vz == 0);
    }

    {
        State s;
        s.y = 20;
        Input in;
        in.flying = true;
        for (int i = 0; i < 20; i++)
            Step(s, in, attr, flat);
        CHECK(Near(s.y, 20.0, 1e-9));
        in.flyUp = true;
        for (int i = 0; i < 20; i++)
            Step(s, in, attr, flat);
        CHECK(s.y > 21.5);
    }

    if (failures == 0)
        std::printf("physics_selftest: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
