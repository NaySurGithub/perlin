#include <perlin/perlin.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace perlin;

namespace {
    constexpr float kSize = 64.0f;
    constexpr double kRange = 24.0;

    struct Blip {
        double dx, dz;
        bool player;
        bool item;
    };

    std::vector<Blip> blips;
    double heading = 0.0;
    bool enabled = true;
}

PERLIN_PLUGIN(ctx) {
    ctx.every(std::chrono::milliseconds(100), [] {
        blips.clear();
        auto self = game::player();
        if (!self)
            return;
        heading = self->yaw * std::numbers::pi / 180.0;
        auto list = game::entities({.radius = kRange});
        if (!list)
            return;
        for (const Entity& e : *list)
            blips.push_back({e.x - self->x, e.z - self->z, e.isPlayer, e.isItem});
    });

    ctx.hud("radar", [](ui::Canvas& g) {
        if (!enabled)
            return;
        const float cx = static_cast<float>(g.width()) - kSize / 2 - 6;
        const float cy = static_cast<float>(g.height()) - kSize / 2 - 40;
        g.rect(cx - kSize / 2, cy - kSize / 2, kSize, kSize, Rgba(0, 0, 0, 120));
        g.border(cx - kSize / 2, cy - kSize / 2, kSize, kSize, Rgba(255, 255, 255, 70));
        g.line(cx - kSize / 2, cy, cx + kSize / 2, cy, Rgba(255, 255, 255, 30));
        g.line(cx, cy - kSize / 2, cx, cy + kSize / 2, Rgba(255, 255, 255, 30));
        g.rect(cx - 1, cy - 1, 3, 3, Rgba::white);
        const double c = std::cos(heading), s = std::sin(heading);
        for (const Blip& b : blips) {
            const double rx = b.dx * c + b.dz * s;
            const double rz = -b.dx * s + b.dz * c;
            const float px = cx + static_cast<float>(rx / kRange) * (kSize / 2);
            const float py = cy - static_cast<float>(rz / kRange) * (kSize / 2);
            if (std::fabs(px - cx) > kSize / 2 - 1 || std::fabs(py - cy) > kSize / 2 - 1)
                continue;
            g.rect(px - 1, py - 1, 2, 2, b.player ? Rgba::red : b.item ? Rgba::aqua : Rgba::yellow);
        }
        g.text(cx, cy - kSize / 2 - 10, "radar", {.color = Rgba::gray, .align = ui::Align::Center});
    });

    ctx.bind("toggle", "Toggle radar", Key::F8, [](bool down) {
        if (down)
            enabled = !enabled;
    });
}
