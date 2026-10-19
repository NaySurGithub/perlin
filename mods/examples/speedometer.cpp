#include <perlin/perlin.hpp>

using namespace perlin;
using namespace std::chrono_literals;

namespace {
    enum class Unit { BlocksPerSecond, KilometersPerHour };
    enum class Corner { TopRight, TopLeft };
}

PERLIN_PLUGIN(ctx) {
    log::info("speedometer loaded");

    // Settings show up on this mod's Settings page (Mods screen -> Speedometer -> Settings) and can be
    // changed while the game runs. Each handle always holds the current value.
    static auto visible = ctx.toggle("visible", "Show speedometer", true,
                                     {.description = "Draws your speed in a corner of the screen.", .icon = icon::item("minecraft:clock")});
    static auto unit = ctx.choice("unit", "Unit", Unit::BlocksPerSecond, {.icon = icon::item("minecraft:compass")});
    static auto corner = ctx.choice("corner", "Corner", Corner::TopRight);
    static auto warn_at = ctx.number("warn_at", "Highlight above", 8.0, 1.0, 40.0, 0.5, {.description = "Speed at which the readout turns gold.", .unit = "b/s"});
    static auto smoothing = ctx.integer("smoothing", "Smoothing", 4, 1, 20, {.description = "Ticks averaged into the readout."});
    ctx.button("reset_peak", "Reset peak", [] { log::info("peak reset"); }, {.icon = icon::item("minecraft:barrier")});

    static double speed = 0.0;
    static double peak = 0.0;

    ctx.on<event::Tick>([](const abi::EventTick&) {
        auto p = game::player();
        if (!p || !p->connected) {
            speed = 0.0;
            return;
        }
        const double now = game::velocity(*p).horizontal_length() * 20.0;
        const double k = 1.0 / std::max(1, smoothing.get());
        speed += (now - speed) * k;
        peak = std::max(peak * 0.995, speed);
    });

    ctx.hud<"speedometer">([](ui::Canvas& g) {
        if (!visible.get() || speed <= 0.01)
            return;
        const bool kmh = unit.get() == Unit::KilometersPerHour;
        const std::string label = kmh ? std::format("{:.1f} km/h", speed * 3.6) : std::format("{:.2f} b/s", speed);
        const float width = ui::text_width(label) + 8.0f;
        const float x = corner.get() == Corner::TopRight ? static_cast<float>(g.width()) - width - 6.0f : 6.0f;
        g.rect(x, 6, width, 14, Rgba(0, 0, 0, 120));
        g.border(x, 6, width, 14, Rgba(255, 255, 255, 40));
        const float fill = static_cast<float>(std::min(1.0, speed / std::max(peak, 1.0)));
        g.rect(x + 1, 18, (width - 2) * fill, 1, Rgba::hex(0x55ff55));
        g.text(x + 4, 10, label, {.color = speed > warn_at.get() ? Rgba::gold : Rgba::white});
    });

    // The parameters are the arguments: `/speed`, `/speed reset`, `/speed unit kmh` all parse by type.
    ctx.command<"speed">("shows your current speed", [](std::optional<Unit> unit_override) {
        const Unit u = unit_override.value_or(unit.get());
        const double shown = u == Unit::KilometersPerHour ? speed * 3.6 : speed;
        (void)act::message("speed {:.2f} {}, peak {:.2f}", shown, u == Unit::KilometersPerHour ? "km/h" : "b/s", peak);
    });

    ctx.bind<"toggle">("Toggle speedometer", Key::F7, [] { (void)visible.set(!visible.get()); });

    // Other mods (or this one) can drive it through intents: Intent("speedometer.action.SET_UNIT").put("unit", Unit::KilometersPerHour)
    ctx.on_intent<"speedometer.action.SET_UNIT">("Changes the speedometer unit", [](const IntentCall& call) {
        if (const auto u = call.get<Unit>("unit"))
            (void)unit.set(*u);
        (void)call.reply(std::pair{"ok", true});
    });

    ctx.on<event::Connected>([&ctx](const abi::EventConnection& e) { ctx.print("speedometer ready on {}", e.server); });

    ctx.on_unload([] { log::info("speedometer unloaded"); });
}
