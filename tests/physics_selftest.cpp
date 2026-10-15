#include "client/player/PlayerPhysics.hpp"

#include <cmath>
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
    BlockShape Query(int x, int y, int z) const override { return fn(x, y, z); }
};

static BlockShape Full() {
    BlockShape s;
    s.count = 1;
    s.boxes[0] = {0, 0, 0, 1, 1, 1};
    return s;
}

static BlockShape Slab(double h) {
    BlockShape s;
    s.count = 1;
    s.boxes[0] = {0, 0, 0, 1, h, 1};
    return s;
}

int main() {
    Attributes attr;

    TestWorld flat;
    flat.fn = [](int, int y, int) { return y < 0 ? Full() : BlockShape{}; };

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
                                for (int k = 0; k < sh.count; k++)
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
        in.forward = 1;
        in.sneak = true;
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
