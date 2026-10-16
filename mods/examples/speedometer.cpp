#include <perlin/perlin.hpp>

using namespace perlin;
using namespace std::chrono_literals;

PERLIN_PLUGIN(ctx) {
    log::info("speedometer loaded");

    static double speed = 0.0;
    static double peak = 0.0;
    static bool visible = true;

    ctx.on_tick([](const event::Tick::Payload&) {
        auto p = game::player();
        if (!p || !p->connected) {
            speed = 0.0;
            return;
        }
        speed = game::velocity(*p).horizontal_length() * 20.0;
        peak = std::max(peak * 0.995, speed);
    });

    ctx.hud("speedometer", [](ui::Canvas& g) {
        if (!visible || speed <= 0.01)
            return;
        const std::string label = std::format("{:.2f} b/s", speed);
        const float width = ui::text_width(label) + 8.0f;
        const float x = static_cast<float>(g.width()) - width - 6.0f;
        g.rect(x, 6, width, 14, Rgba(0, 0, 0, 120));
        g.border(x, 6, width, 14, Rgba(255, 255, 255, 40));
        const float fill = static_cast<float>(std::min(1.0, speed / std::max(peak, 1.0)));
        g.rect(x + 1, 18, (width - 2) * fill, 1, Rgba::hex(0x55ff55));
        g.text(x + 4, 10, label, {.color = speed > 8 ? Rgba::gold : Rgba::white});
    });

    ctx.command("speed", "/speed [reset]", "shows your current speed", [](const CommandCall& call) {
        if (call.arg_or(0, "") == "reset") {
            peak = 0.0;
            call.reply("peak speed reset");
            return;
        }
        call.reply("speed {:.2f} b/s, peak {:.2f} b/s", speed, peak);
    });

    ctx.bind("toggle", "Toggle speedometer", Key::F7, [](bool down) {
        if (down)
            visible = !visible;
    });

    ctx.on<event::Connected>([&ctx](const event::Connected::Payload& e) {
        ctx.print("speedometer ready on {}", e.server);
    });

    ctx.on<event::ChatReceived>([](const event::ChatReceived::Payload& e) {
        if (e.message.starts_with("!pos")) {
            if (auto p = game::player())
                (void)act::message("x={:.1f} y={:.1f} z={:.1f}", p->x, p->y, p->z);
        }
    });

    ctx.on_unload([] { log::info("speedometer unloaded"); });
}
