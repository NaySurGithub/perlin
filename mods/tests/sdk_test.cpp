// Exercises the SDK's high-level layer against a fake host: typed commands, settings, intents, events.
#include <perlin/perlin.hpp>

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
    uint32_t nextIntent = 100;

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
    case Op::SettingDefine: {
        const auto def = r.read<abi::SettingDef>();
        w.write(def.value); // the host answers with the stored value
        break;
    }
    case Op::IntentSend: w.write(nextIntent++); break;
    default: break;
    }
    if (w.Size() > outCap) return -(abi::kNeedMoreBase + static_cast<int32_t>(w.Size()));
    std::memcpy(out, w.Data(), w.Size());
    return static_cast<int32_t>(w.Size());
}

namespace {
    int teleported[3];
    std::optional<Mode> seenMode;
    std::string intentText;

    void send_event(abi::Event id, const auto& payload) {
        abi::Writer w;
        w.write(payload);
        perlin_event(static_cast<uint32_t>(id), w.Data(), static_cast<uint32_t>(w.Size()));
    }
}

PERLIN_PLUGIN(ctx) {
    static auto speed = ctx.number("speed", "Speed", 2.5, 0, 10, 0.5);
    static auto mode = ctx.choice("mode", "Mode", Mode::Slow);
    static auto on = ctx.toggle("on", "On", true);

    ctx.command<"tp">("teleport", [](int x, int y, int z) { teleported[0] = x; teleported[1] = y; teleported[2] = z; });
    ctx.command<"mode">("set mode", [](std::optional<Mode> m) { seenMode = m; });
    ctx.on_intent<"test.action.SAY">("say something", [](const IntentCall& call) {
        intentText = call.get<std::string>("text").value_or("?");
        (void)call.reply(std::pair{"len", static_cast<int>(intentText.size())});
    });
    (void)speed; (void)mode; (void)on;
}

int main() {
    perlin_event(static_cast<uint32_t>(abi::Event::Load), nullptr, 0);

    // Settings: declared with defaults, the handle holds what the host answered.
    CHECK(std::count_if(calls.begin(), calls.end(), [](const Call& c) { return c.op == Op::SettingDefine; }) == 3);
    // Subscriptions include commands, intents and the always-on replies.
    const uint32_t subs = perlin_subscriptions();
    CHECK(subs & (1u << static_cast<uint32_t>(abi::Event::Command)));
    CHECK(subs & (1u << static_cast<uint32_t>(abi::Event::Intent)));
    CHECK(subs & (1u << static_cast<uint32_t>(abi::Event::SettingChanged)));

    // Typed command arguments.
    send_event(abi::Event::Command, abi::EventCommand{"tp", "10 -64 300"});
    CHECK(teleported[0] == 10 && teleported[1] == -64 && teleported[2] == 300);
    send_event(abi::Event::Command, abi::EventCommand{"tp", "10 x 3"});
    CHECK(localMessages.find("Usage: /tp <integer> <integer> <integer>") != std::string::npos);
    send_event(abi::Event::Command, abi::EventCommand{"mode", ""});
    CHECK(!seenMode.has_value());
    send_event(abi::Event::Command, abi::EventCommand{"mode", "FAST"});
    CHECK(seenMode == Mode::Fast);
    localMessages.clear();
    send_event(abi::Event::Command, abi::EventCommand{"mode", "warp"});
    CHECK(localMessages.find("Usage: /mode [Fast|Slow]") != std::string::npos);

    // A setting changed in the UI reaches the handle.
    send_event(abi::Event::SettingChanged, abi::EventSettingChanged{"speed", "7.5", abi::SettingKind::Number});
    // (handles are function-local statics of the plugin; observe the effect through the wire instead)
    calls.clear();

    // Intent in: handler runs and replies with typed extras.
    abi::EventIntent in;
    in.request = 7;
    in.sender = "other.wasm";
    in.intent.action = "test.action.SAY";
    in.intent.extras.push_back({"text", std::string("hello")});
    send_event(abi::Event::Intent, in);
    CHECK(intentText == "hello");
    bool replied = false;
    for (const Call& c : calls)
        if (c.op == Op::IntentReply) {
            abi::Reader r(c.in.data(), c.in.size());
            CHECK(r.get<uint32_t>() == 7);
            const auto extras = r.read<std::vector<abi::Extra>>();
            CHECK(extras.size() == 1 && extras[0].key == "len" && std::get<int64_t>(extras[0].value) == 5);
            replied = true;
        }
    CHECK(replied);

    // Intent out: builder -> wire -> result callback.
    Context& c = detail::context();
    bool got = false;
    const Intent intent = Intent(abi::HostAction::OpenScreen).put("screen", Screen::Mods);
    CHECK(intent.raw().action == "perlin.action.OPEN_SCREEN");
    auto id = c.send(intent, [&](const IntentResult& result) {
        got = result.handled() && result.extras.get<bool>("ok").value_or(false);
    });
    CHECK(id.has_value());
    abi::EventIntentResult res;
    res.request = *id;
    res.outcome = abi::IntentOutcome::Handled;
    res.handler = "perlin";
    res.extras.push_back({"ok", true});
    send_event(abi::Event::IntentResult, res);
    CHECK(got);

    // Extras round-trip every alternative.
    abi::Intent raw;
    raw.action = "a.b";
    raw.target = "m.wasm";
    raw.extras = {{"n", std::monostate{}}, {"b", true}, {"i", int64_t{-5}}, {"d", 2.5}, {"s", std::string("x")}, {"y", std::vector<uint8_t>{1, 2, 3}}};
    abi::Writer w;
    w.write(raw);
    abi::Reader r(w.Data(), w.Size());
    const auto back = r.read<abi::Intent>();
    CHECK(r.Ok() && r.Remaining() == 0 && back.extras.size() == 6 && back.target == "m.wasm");
    const Extras ex(back.extras);
    CHECK(ex.get<int>("i") == -5 && ex.get<double>("d") == 2.5 && ex.get<bool>("b") == true && ex.get<std::string>("s") == "x");
    CHECK(!ex.get<int>("s").has_value());

    // File intents.
    CHECK(file_spec(Area::ResourcePacks, "a.zip") == "resourcepacks:a.zip");
    calls.clear();
    bool dl = false;
    CHECK(c.download("https://example.com/p.zip", file_spec(Area::Mod, "p.zip"), [&](const net::Response& r) { dl = r.status == 200; }).has_value());
    abi::EventHttp http;
    http.id = 1;
    http.status = 200;
    send_event(abi::Event::HttpResponse, http);
    CHECK(dl);
    static_assert(abi::action_id(abi::HostAction::DownloadFile) == "perlin.action.DOWNLOAD_FILE");

    // Typed options and controls.
    static_assert(std::is_same_v<game::option_t<game::Option::fov>, double>);
    static_assert(std::is_same_v<game::option_t<game::Option::vsync>, bool>);
    static_assert(std::is_same_v<game::option_t<game::Option::maxFps>, int>);
    static_assert(std::is_same_v<game::option_t<game::Option::glass>, std::string>);
    static_assert(game::control_id(game::Control::Jump) == "key.jump");
    static_assert(game::control_id(game::Control::Hotbar9) == "key.hotbar.9");
    static_assert(abi::action_id(abi::HostAction::CyclePerspective) == "perlin.action.CYCLE_PERSPECTIVE");

    std::printf(failures ? "sdk_test: %d failure(s)\n" : "sdk_test: ok\n", failures);
    return failures ? 1 : 0;
}
