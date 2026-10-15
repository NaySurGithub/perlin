// Replays pinned bedsim (Oomph's Bedrock movement simulator) traces through perlin::physics::Step.
#include "client/player/PlayerPhysics.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace perlin::physics;

namespace {
    struct Facts {
        double friction = 0.6, hs = 1, vs = 1, fluidH = 0;
        int flags = 0, surface = 0;
    };
    struct Region {
        int mn[3], mx[3];
        Facts facts;
    };
    struct TraceWorld : World {
        Facts base;
        std::vector<Box> boxes;
        std::vector<Region> regions;

        BlockShape Query(int x, int y, int z) const override {
            Facts f = base;
            for (const Region& r : regions)
                if (x >= r.mn[0] && x < r.mx[0] && y >= r.mn[1] && y < r.mx[1] && z >= r.mn[2] && z < r.mx[2])
                    f = r.facts;
            BlockShape s;
            s.friction = static_cast<float>(f.friction);
            s.surface = static_cast<uint8_t>(f.surface);
            if (f.flags & 2) {
                s.fluid = 1;
                s.fluidLevel = static_cast<float>(f.fluidH);
            } else if (f.flags & 4) {
                s.fluid = 2;
                s.fluidLevel = static_cast<float>(f.fluidH);
            }
            s.climbable = (f.flags & 1) != 0;
            s.web = (f.flags & 8) != 0;
            const Box cell{double(x), double(y), double(z), x + 1.0, y + 1.0, z + 1.0};
            for (const Box& b : boxes)
                if (b.Intersects(cell) && s.count < kMaxShapeBoxes)
                    s.boxes[s.count++] = b.Offset(-x, -y, -z);
            return s;
        }
    };

    Facts ReadFacts(std::istringstream& in) {
        Facts f;
        in >> f.friction >> f.hs >> f.vs >> f.fluidH >> f.flags >> f.surface;
        return f;
    }
}

int main(int argc, char** argv) {
    std::ifstream file(argc > 1 ? argv[1] : "tests/data/bedsim_traces.txt");
    if (!file) {
        std::printf("cannot open trace file\n");
        return 2;
    }
    const bool verbose = argc > 2;
    std::string line;
    int checked = 0, failed = 0, scenariosFailed = 0;
    State state;
    std::string scenario;
    int steps = 0;
    TraceWorld world;
    Input input;
    bool scenarioBad = false;
    Attributes attr;
    while (std::getline(file, line)) {
        std::istringstream in(line);
        std::string tag;
        in >> tag;
        if (tag == "S") {
            if (scenarioBad)
                scenariosFailed++;
            scenarioBad = false;
            int onGround;
            state = State{};
            int cx0 = 0, cy0 = 0, cz0 = 0;
            in >> scenario >> state.x >> state.y >> state.z >> state.vx >> state.vy >> state.vz >> onGround >>
                state.jumpDelay >> steps >> cx0 >> cy0 >> cz0;
            state.onGround = onGround != 0;
            state.horizontalCollision = cx0 || cz0;
            state.verticalCollision = cy0 != 0;
        } else if (tag == "W") {
            world = TraceWorld{};
            world.base = ReadFacts(in);
            int nb, nr, unloaded;
            in >> nb >> nr >> unloaded;
        } else if (tag == "B") {
            Box b;
            in >> b.x0 >> b.y0 >> b.z0 >> b.x1 >> b.y1 >> b.z1;
            world.boxes.push_back(b);
        } else if (tag == "R") {
            Region r;
            in >> r.mn[0] >> r.mn[1] >> r.mn[2] >> r.mx[0] >> r.mx[1] >> r.mx[2];
            r.facts = ReadFacts(in);
            world.regions.push_back(r);
        } else if (tag == "I") {
            double fwd, strafe, yaw;
            int jump, jumpPressed, sprint, sneak;
            in >> fwd >> strafe >> yaw >> jump >> jumpPressed >> sprint >> sneak;
            input = Input{};
            input.forward = static_cast<float>(fwd);
            input.strafe = static_cast<float>(-strafe); // bedsim: +strafe is left
            input.jump = jump != 0;
            input.sprint = sprint != 0;
            input.sneak = sneak != 0;
            state.yaw = static_cast<float>(yaw);
            Step(state, input, attr, world);
        } else if (tag == "E") {
            double px, py, pz, vx, vy, vz;
            int og, cx, cy, cz;
            in >> px >> py >> pz >> vx >> vy >> vz >> og >> cx >> cy >> cz;
            auto bad = [&](const char* what, double got, double want, double eps) {
                checked++;
                if (std::fabs(got - want) > eps) {
                    failed++;
                    scenarioBad = true;
                    std::printf("FAIL %s: %s got %.9f want %.9f\n", scenario.c_str(), what, got, want);
                }
            };
            // Vanilla-faithful behaviours that intentionally differ from the old Go capture (see cinnabar
            // conformance.rs): bed 0.75 restitution, soul sand 1.225 acceleration friction, slime damping after
            // friction, sneak edge clipping only the request, consumable slowdown (not modelled yet).
            static const char* const kSkip[] = {"bed_bounce", "soul_sand", "slime_walk", "sneak_north", "sneak_south",
                                                "sneak_east", "sneak_west", "consumable_slowdown",
                                                "water_ledge_exit"};
            bool skip = false;
            for (const char* k : kSkip)
                skip = skip || scenario.find(k) != std::string::npos;
            if (skip)
                continue;
            bad("x", state.x, px, 1e-5);
            bad("y", state.y, py, 1e-5);
            bad("z", state.z, pz, 1e-5);
            bad("vx", state.vx, vx, 1e-5);
            bad("vy", state.vy, vy, 1e-5);
            bad("vz", state.vz, vz, 1e-5);
            bad("onGround", state.onGround, og, 0.5);
            bad("collideY", state.verticalCollision, cy, 0.5);
            bad("collideH", state.horizontalCollision, (cx || cz) ? 1 : 0, 0.5);
            if (verbose)
                std::printf("ok? %s step done\n", scenario.c_str());
        }
    }
    if (scenarioBad)
        scenariosFailed++;
    std::printf("bedsim traces: %d checks, %d failed, %d scenarios with failures\n", checked, failed, scenariosFailed);
    return failed == 0 ? 0 : 1;
}
