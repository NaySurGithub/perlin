// Exercises the SDK's high-level layer against a fake host: typed commands, manifest settings, game actions, files, events.
// Build: tools/perlin_settings.py tests/data/settings_test.json gen/mod_settings.hpp, then compile with -Isdk -Igen.
#include <perlin/perlin.hpp>

#include <mod_settings.hpp>

#include <cstdio>
#include <cstring>
#include <map>

using namespace perlin;

namespace {
    int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                      \
        }                                                                    \
    } while (0)

    struct Call {
        Op op;
        std::vector<uint8_t> in;
    };
    std::vector<Call> calls;
    std::string localMessages;
    std::vector<std::string> unregistered;
    Status denyAll = Status::Ok; // answer every file/network call with this status
    // What the game would answer for SettingList: the manifest's defaults, one saved change.
    std::map<std::string, std::string> hostSettings{{"visible", "true"}, {"unit", "KilometersPerHour"}, {"corner", "TopRight"}, {"warn_at", "8"}, {"smoothing", "4"}};

    enum class Mode { Fast, Slow };
}

extern "C" int32_t perlin_host_call(uint32_t op, const void* in, uint32_t inLen, void* out, uint32_t outCap) {
    const auto o = static_cast<Op>(op);
    const auto* bytes = static_cast<const uint8_t*>(in);
    calls.push_back({o, std::vector<uint8_t>(bytes, bytes + inLen)});
    abi::Reader r(bytes, inLen);
    abi::Writer w;
    switch (o) {
    case Op::LocalMessage: localMessages += r.str() + "\n"; break;
    case Op::SettingList: {
        std::vector<abi::SettingEntry> list;
        for (const auto& [id, value] : hostSettings) list.push_back({id, value});
        w.write(list);
        break;
    }
    case Op::SettingSet: {
        const std::string id = r.str();
        hostSettings[id] = r.str();
        break;
    }
    case Op::Aim: {
        abi::AimState a;
        a.kind = abi::AimKind::Block;
        a.block.hit = true;
        a.block.x = 4, a.block.y = 70, a.block.z = -8;
        a.block.face = abi::Face::North;
        a.block.block = "minecraft:stone";
        a.distance = 3.5;
        w.write(a);
        break;
    }
    case Op::Block: {
        abi::BlockInfo b;
        b.loaded = true;
        b.name = "minecraft:wheat";
        b.states = {{"growth", "5"}, {"open_bit", "true"}};
        w.write(b);
        break;
    }
    case Op::BlockProps: {
        abi::BlockProps p;
        p.known = true;
        p.hardness = 1.5f;
        p.tool = abi::Tool::Pickaxe;
        p.harvestLevel = 1;
        p.displayName = "Stone";
        w.write(p);
        break;
    }
    case Op::BlockEntityData: {
        abi::BlockEntityInfo be;
        be.present = true;
        be.frontText = "hello";
        be.nbt = R"({"note":7})";
        w.write(be);
        break;
    }
    case Op::DisplayName: {
        const auto kind = static_cast<abi::NameKind>(r.get<uint8_t>());
        const std::string id = r.str();
        const std::string text = std::format("{}:{}", kind == abi::NameKind::Entity ? "entity" : "item", id);
        w.raw(text.data(), text.size());
        break;
    }
    case Op::BreakProgress: {
        abi::BreakState b;
        b.active = true;
        b.x = 4, b.y = 70, b.z = -8;
        b.progress = 0.25f;
        w.write(b);
        break;
    }
    case Op::PlayerLook: w.put(90.0f).put(-10.0f); break;
    case Op::SettingGet: {
        const std::string id = r.str();
        if (!hostSettings.count(id)) return static_cast<int32_t>(Status::NotFound);
        w.raw(hostSettings[id].data(), hostSettings[id].size());
        break;
    }
    case Op::CommandUnregister: unregistered.push_back(r.str()); break;
    case Op::CopyFile: case Op::MoveFile: case Op::DownloadFile: case Op::OpenUrl:
        if (denyAll != Status::Ok) return static_cast<int32_t>(denyAll);
        break;
    default: break;
    }
    if (w.Size() > outCap) return -(abi::kNeedMoreBase + static_cast<int32_t>(w.Size()));
    std::memcpy(out, w.Data(), w.Size());
    return static_cast<int32_t>(w.Size());
}

namespace {
    int teleported[3];
    std::optional<Mode> seenMode;
    std::optional<settings::Unit> seenUnit;
    int pressed = 0;

    void send_event(abi::Event id, const auto& payload) {
        abi::Writer w;
        w.write(payload);
        perlin_event(static_cast<uint32_t>(id), w.Data(), static_cast<uint32_t>(w.Size()));
    }
}

namespace { perlin::Context* ctx_for_test = nullptr; }

PERLIN_PLUGIN(ctx) {
    ctx_for_test = &ctx;
    ctx.command<"tp">("teleport", [](int x, int y, int z) { teleported[0] = x; teleported[1] = y; teleported[2] = z; });
    ctx.command<"mode">("set mode", [](std::optional<Mode> m) { seenMode = m; });
    ctx.on_change(settings::unit, [](settings::Unit u) { seenUnit = u; });
    ctx.on_press(settings::reset_peak, [] { pressed++; });
}

int main() {
    perlin_event(static_cast<uint32_t>(abi::Event::Load), nullptr, 0);

    // Settings come from the manifest: the handles read the game's values, typed.
    CHECK(settings::visible.get());
    CHECK(settings::unit == settings::Unit::KilometersPerHour);
    CHECK(settings::corner == settings::Corner::TopRight);
    CHECK(settings::warn_at.get() == 8.0);
    CHECK(settings::smoothing.get() == 4);
    CHECK(setting<double>("warn_at") == 8.0);
    const uint32_t subs = perlin_subscriptions();
    CHECK(subs & (1u << static_cast<uint32_t>(abi::Event::Command)));
    CHECK(subs & (1u << static_cast<uint32_t>(abi::Event::SettingChanged)));

    // A change on the settings page updates the handle and runs on_change with the typed value.
    send_event(abi::Event::SettingChanged, abi::EventSettingChanged{"unit", "BlocksPerSecond", abi::SettingKind::Choice});
    CHECK(settings::unit == settings::Unit::BlocksPerSecond && seenUnit == settings::Unit::BlocksPerSecond);
    send_event(abi::Event::SettingChanged, abi::EventSettingChanged{"warn_at", "12.5", abi::SettingKind::Number});
    CHECK(settings::warn_at.get() == 12.5);
    send_event(abi::Event::SettingChanged, abi::EventSettingChanged{"reset_peak", "pressed", abi::SettingKind::Button});
    CHECK(pressed == 1);
    // Setting from the mod goes to the game and the handle.
    CHECK(settings::visible.set(false).has_value());
    CHECK(!settings::visible.get() && hostSettings["visible"] == "false");

    // Typed command arguments.
    send_event(abi::Event::Command, abi::EventCommand{"tp", "10 -64 300"});
    CHECK(teleported[0] == 10 && teleported[1] == -64 && teleported[2] == 300);
    send_event(abi::Event::Command, abi::EventCommand{"tp", "10 x 3"});
    CHECK(localMessages.find("Usage: /tp <integer> <integer> <integer>") != std::string::npos);
    send_event(abi::Event::Command, abi::EventCommand{"mode", "FAST"});
    CHECK(seenMode == Mode::Fast);
    localMessages.clear();
    send_event(abi::Event::Command, abi::EventCommand{"mode", "warp"});
    CHECK(localMessages.find("Usage: /mode [Fast|Slow]") != std::string::npos);

    // Game actions are plain typed calls.
    calls.clear();
    CHECK(act::open_screen(Screen::Mods).has_value());
    CHECK(act::open_settings().has_value());
    CHECK(act::screenshot().has_value());
    CHECK(act::copy_to_clipboard("hello").has_value());
    CHECK(calls.size() == 4 && calls[0].op == Op::OpenScreen && calls[1].op == Op::OpenModSettings);
    {
        abi::Reader r(calls[0].in.data(), calls[0].in.size());
        CHECK(r.read<abi::Screen>() == Screen::Mods);
    }

    // Files: typed locations, permission failures come back as a Denied error instead of a string.
    calls.clear();
    CHECK(fs::copy(fs::in(Area::Mod, "pack.zip"), fs::in(Area::ResourcePacks, "pack.zip")).has_value());
    {
        abi::Reader r(calls[0].in.data(), calls[0].in.size());
        const auto from = r.read<abi::FileLocation>();
        const auto to = r.read<abi::FileLocation>();
        CHECK(calls[0].op == Op::CopyFile && from.area == Area::Mod && to.area == Area::ResourcePacks && to.path == "pack.zip");
    }
    denyAll = Status::Denied;
    const auto denied = fs::move(fs::in(Area::Mod, "a"), fs::in(Area::Screenshots, "a"));
    CHECK(!denied && denied.error().denied());
    CHECK(!act::open_url("https://example.com").has_value());
    denyAll = Status::Ok;

    // Downloads complete through the HttpResponse event.
    bool dl = false;
    auto id = net::download("https://example.com/p.zip", fs::in(Area::Mod, "p.zip"), [&](const net::Response& r) { dl = r.status == 200; });
    CHECK(id.has_value());
    abi::EventHttp http;
    http.id = *id;
    http.status = 200;
    send_event(abi::Event::HttpResponse, http);
    CHECK(dl);

    // Typed options and controls.
    static_assert(std::is_same_v<game::option_t<game::Option::fov>, double>);
    static_assert(std::is_same_v<game::option_t<game::Option::vsync>, bool>);
    static_assert(std::is_same_v<game::option_t<game::Option::maxFps>, int>);
    static_assert(game::control_id(game::Control::Jump) == "key.jump");

    // What the crosshair is on, resolved in one call.
    {
        const game::Target t = game::target();
        const auto* b = std::get_if<game::BlockTarget>(&t);
        CHECK(b != nullptr);
        if (b) {
            CHECK(b->pos == BlockPos(4, 70, -8) && b->face == Face::North && b->distance == 3.5);
            CHECK(b->block.name == "minecraft:wheat" && b->block.state_int("growth") == 5 && b->block.state_bool("open_bit"));
            CHECK(!b->block.state("missing") && b->block.state_int("missing", 9) == 9);
            CHECK(b->props.tool == Tool::Pickaxe && b->props.harvestLevel == 1 && b->display_name == "Stone");
            CHECK(b->entity().present && b->entity().frontText == "hello");
            auto tag = json::parse(b->entity().nbt);
            CHECK(tag && (*tag)["note"].integer() == 7);
        }
    }
    CHECK(game::display_name(NameKind::Entity, "minecraft:cow") == "entity:minecraft:cow");
    CHECK(game::display_name(NameKind::Item, "minecraft:apple") == "item:minecraft:apple");
    {
        const auto state = game::break_state();
        CHECK(state && state->active && state->progress == 0.25f && state->y == 70);
        const auto look = game::look();
        CHECK(look && look->yaw == 90.0f && look->pitch == -10.0f);
        const auto text = setting_text("warn_at");
        CHECK(text && *text == "8");
        CHECK(!setting_text("nope"));
    }
    ctx_for_test->remove_command<"tp">();
    CHECK(unregistered.size() == 1 && unregistered[0] == "tp");

    // Entity data is plain data: attributes and properties travel with it.
    {
        abi::Entity e;
        e.attributes.push_back({"minecraft:health", 0, 5, 20});
        e.properties.emplace_back("minecraft:variant", 2.0f);
        e.propertyText.emplace_back("minecraft:mood", "calm");
        abi::Writer w;
        w.write(e);
        abi::Reader r(w.Data(), w.Size());
        const auto back = r.read<abi::Entity>();
        CHECK(r.Ok() && back.attributes.size() == 1 && back.attributes[0].max == 20 && back.properties[0].second == 2.0f && back.propertyText[0].second == "calm");
    }
    CHECK(std::format("{}", abi::Tool::Pickaxe) == "Pickaxe" && std::format("{}", abi::NameKind::Entity) == "Entity"); // every perlin::abi enumeration formats by name

    std::printf(failures ? "sdk_test: %d failure(s)\n" : "sdk_test: ok\n", failures);
    return failures ? 1 : 0;
}
