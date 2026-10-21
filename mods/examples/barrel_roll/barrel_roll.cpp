#include <perlin/perlin.hpp>

using namespace perlin;

namespace {
    float angle = 0.0f;
    bool rolling = false;
}

PERLIN_PLUGIN(ctx) {
    ctx.bind<"roll">("Barrel roll", Key::R, [] {
        auto p = game::player();
        if (p && p->gliding)
            rolling = true;
    });

    ctx.on_frame([](const abi::EventFrame& f) {
        if (!rolling)
            return;
        angle += 360.0f * 1.5f * f.dt;
        if (angle >= 360.0f) {
            angle = 0.0f;
            rolling = false;
            return;
        }
        (void)game::camera({.roll = angle, .ticks = 2});
        (void)game::model_pose({.yaw = angle, .ticks = 2});
    });
}
