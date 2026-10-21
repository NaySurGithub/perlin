// Elytra barrel roll: R while gliding spins the camera and the player's model one full turn around the flight direction.
// It only uses the low-level range (perlin::adv): camera roll and model pose, each refreshed every frame so they lapse on
// their own if the mod ever stops.
#include <perlin/perlin.hpp>

using namespace perlin;

namespace {
    float angle = 0.0f;   // degrees into the current roll
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
        angle += 360.0f * 1.5f * f.dt; // a roll takes about two thirds of a second
        if (angle >= 360.0f) {
            angle = 0.0f;
            rolling = false;
            return;
        }
        (void)adv::camera({.roll = angle, .ticks = 2});
        (void)adv::model_pose({.yaw = angle, .ticks = 2});
    });
}
