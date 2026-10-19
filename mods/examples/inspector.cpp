#include <perlin/perlin.hpp>

#include <algorithm>
#include <vector>

using namespace perlin;

namespace {
    struct Snapshot {
        std::string looking_at = "nothing";
        std::string held = "empty hand";
        std::string biome_line;
        std::string screen_line;
        size_t nearby = 0;
        size_t blocks_found = 0;
        bool connected = false;
    };

    Snapshot snapshot;
}

PERLIN_PLUGIN(ctx) {
    log::info("inspector loaded");

    ctx.every(std::chrono::milliseconds(250), [] {
        Snapshot next;
        if (auto player = game::player())
            next.connected = player->connected;

        if (auto aim = game::aim()) {
            if (aim->kind == abi::AimKind::Block && aim->block.hit) {
                const BlockPos pos{aim->block.x, aim->block.y, aim->block.z};
                next.looking_at = std::format("{} at {}", short_name(aim->block.block), pos);
                if (auto info = game::block(pos); info && info->loaded) {
                    next.biome_line = std::format("biome {}  liquid {}  states {}", info->biome, info->liquid, info->states.size());
                    game::FindOptions find{.name = aim->block.block, .radius = 6, .max_results = 512, .exact = true};
                    if (auto found = game::find_blocks(pos, find))
                        next.blocks_found = found->size();
                }
            } else if (aim->kind == abi::AimKind::Entity) {
                next.looking_at = std::format("{} #{}", short_name(aim->entityType), aim->entity);
            }
        }

        if (auto inv = game::inventory()) {
            const abi::Item& stack = inv->slots[static_cast<size_t>(std::clamp(inv->selected, 0, 8))];
            if (!stack.Empty())
                next.held = std::format("{} x{}", short_name(stack.name), stack.count);
        }

        if (auto entities = game::entities({.radius = 16.0}))
            next.nearby = entities->size();

        if (auto screen = game::screen())
            next.screen_line = std::format("{}x{} gui, {:.0f} fps, {}", screen->guiWidth, screen->guiHeight, screen->fps, screen->platform);

        snapshot = std::move(next);
    });

    ctx.hud("inspector", [](ui::Canvas& g) {
        const std::vector<std::string> lines = {
            std::format("Looking at: {}", snapshot.looking_at),
            std::format("Holding: {}", snapshot.held),
            snapshot.biome_line,
            std::format("Same block within 6: {}", snapshot.blocks_found),
            std::format("Entities within 16: {}", snapshot.nearby),
            snapshot.screen_line,
        };
        float width = 0;
        for (const auto& line : lines)
            width = std::max(width, ui::text_width(line));
        width += 10;
        const float height = 4 + 11.0f * static_cast<float>(lines.size());
        g.gradient(4, 4, width, height, Rgba(0, 0, 0, 170), Rgba(20, 20, 40, 170));
        g.border(4, 4, width, height, Rgba(255, 255, 255, 60));
        float y = 8;
        for (const auto& line : lines) {
            g.text(9, y, line, {.color = line.starts_with("Looking") ? Rgba::gold : Rgba::white});
            y += 11;
        }
    });

    ctx.command<"inspect">("prints what you are looking at", [](const CommandCall& call) {
        call.reply("looking at {}, holding {}", snapshot.looking_at, snapshot.held);
    });
}
