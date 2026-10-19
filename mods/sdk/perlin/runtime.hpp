#pragma once
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <utility>
#include <vector>

#include "perlin/abi.hpp"
#include "perlin/act.hpp"
#include "perlin/game.hpp"
#include "perlin/io.hpp"
#include "perlin/rpc.hpp"
#include "perlin/types.hpp"
#include "perlin/ui.hpp"

namespace perlin {
    enum class Flow { pass, cancel };

    namespace log {
        inline void write(int level, std::string_view text) {
            abi::Writer w;
            w.put<int32_t>(level).str(text);
            (void)detail::call_void(Op::Log, w);
        }
        template <class... Args> void debug(std::format_string<Args...> fmt, Args&&... args) { write(0, std::format(fmt, std::forward<Args>(args)...)); }
        template <class... Args> void info(std::format_string<Args...> fmt, Args&&... args) { write(1, std::format(fmt, std::forward<Args>(args)...)); }
        template <class... Args> void warn(std::format_string<Args...> fmt, Args&&... args) { write(2, std::format(fmt, std::forward<Args>(args)...)); }
        template <class... Args> void error(std::format_string<Args...> fmt, Args&&... args) { write(3, std::format(fmt, std::forward<Args>(args)...)); }
    }

    // ---------------------------------------------------------------------------------------------
    // Events. `event::Tick` and `abi::Event::Tick` both work as the argument of `ctx.on`; the handler
    // receives that event's payload type, taken from the ABI table, so it cannot be the wrong type.
    // ---------------------------------------------------------------------------------------------
    template <abi::Event E>
    struct EventTag {
        using Payload = abi::payload_t<E>;
        static constexpr abi::Event id = E;
    };

    namespace event {
#define PERLIN_EVENT_TAG(Name) using Name = EventTag<abi::Event::Name>
        PERLIN_EVENT_TAG(Tick);
        PERLIN_EVENT_TAG(Frame);
        PERLIN_EVENT_TAG(ChatReceived);
        PERLIN_EVENT_TAG(ChatSending);
        PERLIN_EVENT_TAG(Connected);
        PERLIN_EVENT_TAG(Disconnected);
        PERLIN_EVENT_TAG(Key);
        PERLIN_EVENT_TAG(Command);
        PERLIN_EVENT_TAG(Damage);
        PERLIN_EVENT_TAG(Death);
        PERLIN_EVENT_TAG(Respawned);
        PERLIN_EVENT_TAG(DimensionChanged);
        PERLIN_EVENT_TAG(ScreenChanged);
        PERLIN_EVENT_TAG(SlotChanged);
        PERLIN_EVENT_TAG(EntityAdded);
        PERLIN_EVENT_TAG(EntityRemoved);
        PERLIN_EVENT_TAG(Sound);
        PERLIN_EVENT_TAG(HttpResponse);
        PERLIN_EVENT_TAG(Packet);
        PERLIN_EVENT_TAG(Title);
        PERLIN_EVENT_TAG(SettingChanged);
        PERLIN_EVENT_TAG(Intent);
        PERLIN_EVENT_TAG(IntentResult);
#undef PERLIN_EVENT_TAG
    }

    template <class T>
    concept EventTagLike = requires {
        typename T::Payload;
        { T::id } -> std::convertible_to<abi::Event>;
    };

    enum class Key : int {
        None = 0,
        A = 'A', B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Num0 = '0', Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        Space = 0x20, Tab = 0x09, Enter = 0x0D, Shift = 0x10, Control = 0x11, Alt = 0x12,
        Left = 0x25, Up, Right, Down, Home = 0x24, End = 0x23, PageUp = 0x21, PageDown = 0x22, Delete = 0x2E,
        MouseMiddle = 1000, MouseLeft, MouseRight,
    };

    // The game's key id for a Key, computed from the enumerator: letters and digits by value, the rest by name.
    inline std::string key_id(Key key) {
        const int k = static_cast<int>(key);
        if (k >= 'A' && k <= 'Z') return std::string("key.keyboard.") + static_cast<char>(k - 'A' + 'a');
        if (k >= '0' && k <= '9') return std::string("key.keyboard.") + static_cast<char>(k);
        if (k >= 0x70 && k <= 0x7B) return "key.keyboard.f" + std::to_string(k - 0x70 + 1);
        switch (key) {
        case Key::Space: return "key.keyboard.space";
        case Key::Tab: return "key.keyboard.tab";
        case Key::Enter: return "key.keyboard.enter";
        case Key::Shift: return "key.keyboard.shift";
        case Key::Control: return "key.keyboard.control";
        case Key::Alt: return "key.keyboard.alt";
        case Key::Left: return "key.keyboard.left";
        case Key::Up: return "key.keyboard.up";
        case Key::Right: return "key.keyboard.right";
        case Key::Down: return "key.keyboard.down";
        case Key::Home: return "key.keyboard.home";
        case Key::End: return "key.keyboard.end";
        case Key::PageUp: return "key.keyboard.page.up";
        case Key::PageDown: return "key.keyboard.page.down";
        case Key::Delete: return "key.keyboard.delete";
        case Key::MouseMiddle: return "key.mouse.middle";
        case Key::MouseLeft: return "key.mouse.left";
        case Key::MouseRight: return "key.mouse.right";
        default: return {};
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Icons for settings and intents: "item:minecraft:feather", "texture:textures/items/apple",
    // "file:icons/speed.png" (relative to the mod's data folder).
    // ---------------------------------------------------------------------------------------------
    namespace icon {
        inline std::string item(std::string_view id) { return "item:" + std::string(id); }
        inline std::string texture(std::string_view path) { return "texture:" + std::string(path); }
        inline std::string file(std::string_view relative) { return "file:" + std::string(relative); }
        inline constexpr std::string_view none = {};
    }

    // The mod's own icon in the Mods screen, replacing the one from the manifest. `relative_png` lives in the
    // mod's data folder.
    inline Result<void> set_mod_icon(std::string_view relative_png) {
        abi::Writer w;
        w.write(relative_png);
        return detail::call_void(Op::IconSet, w);
    }

    // ---------------------------------------------------------------------------------------------
    // Commands: `ctx.command<"tp">("teleport", [](int x, int y, int z) { ... })`. The parameter types are the
    // argument grammar: text, numbers, bool, enumerations (by name) and std::optional<T> for the optional tail.
    // ---------------------------------------------------------------------------------------------
    struct CommandCall {
        std::string name;
        std::string raw_args;
        std::vector<std::string> args;

        size_t size() const { return args.size(); }
        bool empty() const { return args.empty(); }
        const std::string& operator[](size_t i) const { return args[i]; }
        std::string_view arg_or(size_t i, std::string_view fallback) const { return i < args.size() ? std::string_view(args[i]) : fallback; }

        template <class... A>
        void reply(std::format_string<A...> fmt, A&&... a) const {
            (void)act::message(fmt, std::forward<A>(a)...);
        }
    };

    namespace detail_cmd {
        template <class T>
        struct is_optional : std::false_type {};
        template <class T>
        struct is_optional<std::optional<T>> : std::true_type {};

        template <class F>
        struct signature : signature<decltype(&F::operator())> {};
        template <class C, class R, class... A>
        struct signature<R (C::*)(A...) const> {
            using args = std::tuple<std::remove_cvref_t<A>...>;
        };
        template <class C, class R, class... A>
        struct signature<R (C::*)(A...)> {
            using args = std::tuple<std::remove_cvref_t<A>...>;
        };
        template <class R, class... A>
        struct signature<R (*)(A...)> {
            using args = std::tuple<std::remove_cvref_t<A>...>;
        };

        template <class T>
        bool parse_value(std::string_view token, T& out) {
            if constexpr (std::is_same_v<T, bool>) {
                if (token == "true" || token == "on" || token == "1" || token == "yes") out = true;
                else if (token == "false" || token == "off" || token == "0" || token == "no") out = false;
                else return false;
                return true;
            } else if constexpr (std::is_same_v<T, std::string>) {
                out = std::string(token);
                return true;
            } else if constexpr (std::is_enum_v<T>) {
                const auto value = meta::enum_cast_nocase<T>(token);
                if (!value) return false;
                out = *value;
                return true;
            } else if constexpr (std::is_floating_point_v<T>) {
                const auto r = std::from_chars(token.data(), token.data() + token.size(), out);
                return r.ec == std::errc{} && r.ptr == token.data() + token.size();
            } else {
                static_assert(std::is_integral_v<T>, "unsupported command argument type");
                const auto r = std::from_chars(token.data(), token.data() + token.size(), out);
                return r.ec == std::errc{} && r.ptr == token.data() + token.size();
            }
        }

        template <class T>
        bool parse_arg(const CommandCall& call, size_t index, T& out) {
            if constexpr (is_optional<T>::value) {
                if (index >= call.args.size()) {
                    out = std::nullopt;
                    return true;
                }
                typename T::value_type inner{};
                if (!parse_value<typename T::value_type>(call.args[index], inner)) return false;
                out = std::move(inner);
                return true;
            } else {
                return index < call.args.size() && parse_value<T>(call.args[index], out);
            }
        }

        template <class T>
        std::string describe() {
            if constexpr (is_optional<T>::value) {
                return "[" + describe<typename T::value_type>().substr(1);
            } else if constexpr (std::is_same_v<T, bool>) {
                return "<on|off>";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "<text>";
            } else if constexpr (std::is_enum_v<T>) {
                std::string out = "<";
                for (size_t i = 0; i < meta::enum_count<T>; i++) {
                    if (i) out += '|';
                    out += meta::enum_names<T>[i];
                }
                return out + ">";
            } else if constexpr (std::is_floating_point_v<T>) {
                return "<number>";
            } else {
                return "<integer>";
            }
        }

        // `]` closes optional arguments; the describe() of an optional ends with '>' that becomes ']'.
        template <class T>
        std::string describe_closed() {
            std::string text = describe<T>();
            if (is_optional<T>::value && !text.empty()) text.back() = ']';
            return text;
        }

        template <class Tuple, size_t... I>
        std::string usage_of(std::index_sequence<I...>) {
            std::string out;
            ((out += " " + describe_closed<std::tuple_element_t<I, Tuple>>()), ...);
            return out;
        }

        template <class F>
        constexpr bool takes_call() {
            using Args = typename signature<std::remove_cvref_t<F>>::args;
            if constexpr (std::tuple_size_v<Args> == 1)
                return std::is_same_v<std::tuple_element_t<0, Args>, CommandCall>;
            else
                return false;
        }
    }

    // Folders a file intent can name. `Mod` is the mod's own data folder; the others are the game's.
    // Files may be copied or moved out of `Mod` (and copied out of `Screenshots`) into any area; nothing already in
    // a game folder is overwritten. Needs the "files" permission (downloads also need "network").
    enum class Area { Mod, Screenshots, ResourcePacks, ShaderPacks };

    inline std::string file_spec(Area area, std::string_view relative) {
        constexpr std::array<std::string_view, 4> names{"mod", "screenshots", "resourcepacks", "shaderpacks"};
        return std::string(names[static_cast<size_t>(area)]) + ":" + std::string(relative);
    }

    // ---------------------------------------------------------------------------------------------
    // Extras and intents.
    // ---------------------------------------------------------------------------------------------
    class Extras {
    public:
        Extras() = default;
        explicit Extras(std::vector<abi::Extra> raw) : items(std::move(raw)) {}

        bool has(std::string_view key) const { return find(key) != nullptr; }
        size_t size() const { return items.size(); }
        const std::vector<abi::Extra>& raw() const { return items; }

        // `get<int>("count")`, `get<Screen>("screen")`, `get<std::string>("text")`. Empty when absent or of another type.
        template <class T>
        std::optional<T> get(std::string_view key) const {
            const abi::Extra* e = find(key);
            if (!e) return std::nullopt;
            if constexpr (std::is_same_v<T, bool>) {
                if (const auto* v = std::get_if<bool>(&e->value)) return *v;
            } else if constexpr (std::is_enum_v<T>) {
                if (const auto* v = std::get_if<int64_t>(&e->value)) return static_cast<T>(*v);
                if (const auto* s = std::get_if<std::string>(&e->value)) return meta::enum_cast_nocase<T>(*s);
            } else if constexpr (std::is_integral_v<T>) {
                if (const auto* v = std::get_if<int64_t>(&e->value)) return static_cast<T>(*v);
            } else if constexpr (std::is_floating_point_v<T>) {
                if (const auto* v = std::get_if<double>(&e->value)) return static_cast<T>(*v);
                if (const auto* v = std::get_if<int64_t>(&e->value)) return static_cast<T>(*v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                if (const auto* v = std::get_if<std::string>(&e->value)) return *v;
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                if (const auto* v = std::get_if<std::vector<uint8_t>>(&e->value)) return *v;
            }
            return std::nullopt;
        }

        template <class T>
        T get_or(std::string_view key, T fallback) const {
            return get<T>(key).value_or(std::move(fallback));
        }

    private:
        const abi::Extra* find(std::string_view key) const {
            for (const abi::Extra& e : items)
                if (e.key == key) return &e;
            return nullptr;
        }
        std::vector<abi::Extra> items;
    };

    namespace detail_intent {
        template <class T>
        abi::ExtraValue to_value(const T& v) {
            if constexpr (std::is_same_v<T, bool>) return v;
            else if constexpr (std::is_enum_v<T>) return static_cast<int64_t>(v);
            else if constexpr (std::is_integral_v<T>) return static_cast<int64_t>(v);
            else if constexpr (std::is_floating_point_v<T>) return static_cast<double>(v);
            else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) return v;
            else return std::string(std::string_view(v));
        }
    }

    // A message to the game or to another mod: an action, an optional target mod, and typed extras.
    //   Intent(abi::HostAction::OpenScreen).put("screen", Screen::Mods)
    //   Intent("shop.action.BUY").to("shop.wasm").put("item", "minecraft:apple").put("count", 3)
    class Intent {
    public:
        explicit Intent(std::string action) { data.action = std::move(action); }
        explicit Intent(abi::HostAction action) { data.action = std::string(abi::action_id(action)); }

        Intent& to(std::string mod) {
            data.target = std::move(mod);
            return *this;
        }
        template <class T>
        Intent& put(std::string key, const T& value) {
            data.extras.push_back({std::move(key), detail_intent::to_value(value)});
            return *this;
        }
        const abi::Intent& raw() const { return data; }

    private:
        abi::Intent data;
    };

    // What a handler sees: the action, who sent it, the extras, and a way to answer.
    class IntentCall {
    public:
        IntentCall(uint32_t request_id, std::string from, abi::Intent raw) : request(request_id), sender_(std::move(from)), intent_(std::move(raw)), extras_(intent_.extras) {}

        std::string_view action() const { return intent_.action; }
        const std::string& sender() const { return sender_; }
        const Extras& extras() const { return extras_; }
        template <class T>
        std::optional<T> get(std::string_view key) const { return extras_.get<T>(key); }

        // Sends extras back to the sender; they arrive in its IntentResult.
        template <class... Pairs>
        Result<void> reply(Pairs&&... pairs) const {
            std::vector<abi::Extra> out;
            (out.push_back({std::string(pairs.first), detail_intent::to_value(pairs.second)}), ...);
            abi::Writer w;
            w.write(request, out);
            return detail::call_void(Op::IntentReply, w);
        }

    private:
        uint32_t request;
        std::string sender_;
        abi::Intent intent_;
        Extras extras_;
    };

    struct IntentResult {
        abi::IntentOutcome outcome = abi::IntentOutcome::Unhandled;
        std::string handler;
        Extras extras;
        bool handled() const { return outcome == abi::IntentOutcome::Handled; }
    };

    // Who would handle an action right now: the game ("perlin") and any mods that registered for it.
    inline Result<std::vector<abi::IntentHandler>> intent_handlers(std::string_view action) {
        abi::Writer w;
        w.write(action);
        return detail::call_list<abi::IntentHandler>(Op::IntentQuery, w);
    }
    inline Result<std::vector<abi::IntentHandler>> intent_handlers(abi::HostAction action) { return intent_handlers(abi::action_id(action)); }

    // ---------------------------------------------------------------------------------------------
    // Settings. Declared once; the game lists them on the mod's Settings page, stores them, and tells the mod
    // when one changes. A `Setting<T>` handle always holds the current value.
    // ---------------------------------------------------------------------------------------------
    struct SettingOptions {
        std::string description;
        std::string icon; // icon::item(...), icon::texture(...), icon::file(...)
        std::string unit;
        bool restart = false; // reload the mod when the page closes
    };

    namespace detail_setting {
        template <class T>
        std::string to_text(const T& v) {
            if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
            else if constexpr (std::is_same_v<T, std::string>) return v;
            else if constexpr (std::is_enum_v<T>) return std::string(meta::enum_name(v));
            else return std::format("{}", v);
        }
    }

    template <class T>
    class Setting {
    public:
        Setting() : state(std::make_shared<T>()) {}
        const T& get() const { return *state; }
        const T& operator*() const { return *state; }
        operator const T&() const { return *state; }
        const std::string& id() const { return id_; }

        // Changes the value from the mod side (the game stores it and shows it on the settings page).
        Result<void> set(const T& value) {
            abi::Writer w;
            w.write(id_, detail_setting::to_text(value));
            auto r = detail::call_void(Op::SettingSet, w);
            if (r) *state = value;
            return r;
        }

    private:
        friend class Context;
        Setting(std::shared_ptr<T> s, std::string id) : state(std::move(s)), id_(std::move(id)) {}
        std::shared_ptr<T> state;
        std::string id_;
    };

    // ---------------------------------------------------------------------------------------------
    // The context a plugin's entry point receives.
    // ---------------------------------------------------------------------------------------------
    enum class TimerId : uint32_t {};

    class Context {
    public:
        using Duration = std::chrono::duration<double>;

        // ---- Events ----
        template <EventTagLike Tag, class Fn>
        void on(Fn&& fn) {
            add_handler(Tag::id, wrap<typename Tag::Payload>(std::forward<Fn>(fn)));
        }
        template <abi::Event E, class Fn>
        void on(Fn&& fn) {
            on<EventTag<E>>(std::forward<Fn>(fn));
        }

        template <class Fn> void on_chat(Fn&& fn) { on<event::ChatReceived>(std::forward<Fn>(fn)); }
        template <class Fn> void on_tick(Fn&& fn) { on<event::Tick>(std::forward<Fn>(fn)); }
        template <class Fn> void on_frame(Fn&& fn) { on<event::Frame>(std::forward<Fn>(fn)); }
        template <class Fn> void on_unload(Fn&& fn) { unload_handlers.push_back(std::forward<Fn>(fn)); }

        // ---- Timers (std::chrono durations; `20_ticks` works) ----
        template <class Rep, class Period, class Fn>
        TimerId after(std::chrono::duration<Rep, Period> delay, Fn&& fn) {
            return add_timer(std::chrono::duration_cast<Duration>(delay), Duration::zero(), std::forward<Fn>(fn));
        }
        template <class Rep, class Period, class Fn>
        TimerId every(std::chrono::duration<Rep, Period> period, Fn&& fn) {
            const auto p = std::chrono::duration_cast<Duration>(period);
            return add_timer(p, p, std::forward<Fn>(fn));
        }
        void cancel(TimerId timer) {
            for (auto& t : timers)
                if (t.id == timer) t.alive = false;
        }

        // ---- Commands ----
        // Typed: the handler's parameters are the arguments. Also accepts `(const CommandCall&)` for raw access.
        template <meta::fixed_string Name, class Fn>
        void command(std::string_view description, Fn&& fn) {
            using Fun = std::remove_cvref_t<Fn>;
            const std::string name(Name.view());
            if constexpr (detail_cmd::takes_call<Fun>()) {
                add_command(name, "", std::string(description), [f = std::forward<Fn>(fn)](const CommandCall& c) mutable { f(c); });
            } else {
                using Args = typename detail_cmd::signature<Fun>::args;
                const std::string usage = detail_cmd::usage_of<Args>(std::make_index_sequence<std::tuple_size_v<Args>>{});
                add_command(name, usage, std::string(description), [f = std::forward<Fn>(fn), usage, name](const CommandCall& c) mutable {
                    Args values{};
                    const bool ok = [&]<size_t... I>(std::index_sequence<I...>) {
                        return (detail_cmd::parse_arg<std::tuple_element_t<I, Args>>(c, I, std::get<I>(values)) && ...);
                    }(std::make_index_sequence<std::tuple_size_v<Args>>{});
                    if (!ok) {
                        c.reply("Usage: /{}{}", name, usage);
                        return;
                    }
                    std::apply(f, std::move(values));
                });
            }
        }

        template <class Fn>
        void command(std::string name, std::string usage, std::string description, Fn&& fn) {
            add_command(name, usage, description, std::forward<Fn>(fn));
        }

        // ---- Key bindings (appear in Controls) ----
        template <meta::fixed_string Id, class Fn>
        void bind(std::string_view label, Key default_key, Fn&& fn) {
            bind(std::string(Id.view()), std::string(label), default_key, std::forward<Fn>(fn));
        }
        template <class Fn>
        void bind(std::string id, std::string label, Key default_key, Fn&& fn) {
            abi::Writer w;
            w.write(id, label, key_id(default_key));
            (void)detail::call_void(Op::KeyRegister, w);
            keys[id] = wrap_key(std::forward<Fn>(fn));
        }

        // ---- HUD layers ----
        template <meta::fixed_string Layer, class Fn>
        void hud(Fn&& fn, int z = 0) {
            hud(std::string(Layer.view()), std::forward<Fn>(fn), z);
        }
        template <class Fn>
        void hud(std::string layer, Fn&& fn, int z = 0) {
            layers.push_back({std::move(layer), z, std::forward<Fn>(fn), {}});
        }

        template <class... Args>
        void print(std::format_string<Args...> fmt, Args&&... args) { (void)act::message(fmt, std::forward<Args>(args)...); }

        // ---- Settings ----
        Setting<bool> toggle(std::string id, std::string label, bool initial, SettingOptions options = {}) {
            return declare<bool>(abi::SettingKind::Toggle, std::move(id), std::move(label), initial ? "true" : "false", options, 0, 1, 0, {});
        }
        template <std::integral T = int>
        Setting<T> integer(std::string id, std::string label, T initial, T min, T max, SettingOptions options = {}) {
            return declare<T>(abi::SettingKind::Integer, std::move(id), std::move(label), std::to_string(initial), options, static_cast<double>(min),
                              static_cast<double>(max), 1, {});
        }
        Setting<double> number(std::string id, std::string label, double initial, double min, double max, double step = 0, SettingOptions options = {}) {
            return declare<double>(abi::SettingKind::Number, std::move(id), std::move(label), std::format("{}", initial), options, min, max, step, {});
        }
        // A choice between the enumerators of E; the page shows their names.
        template <meta::Enumeration E>
        Setting<E> choice(std::string id, std::string label, E initial, SettingOptions options = {}) {
            std::vector<std::string> names;
            for (size_t i = 0; i < meta::enum_count<E>; i++) names.emplace_back(meta::enum_names<E>[i]);
            return declare<E>(abi::SettingKind::Choice, std::move(id), std::move(label), std::string(meta::enum_name(initial)), options, 0, 1, 0, std::move(names));
        }
        Setting<std::string> choice(std::string id, std::string label, std::vector<std::string> values, std::string initial, SettingOptions options = {}) {
            return declare<std::string>(abi::SettingKind::Choice, std::move(id), std::move(label), std::move(initial), options, 0, 1, 0, std::move(values));
        }
        template <class Fn>
        void button(std::string id, std::string label, Fn&& fn, SettingOptions options = {}) {
            define(abi::SettingKind::Button, id, std::move(label), "", options, 0, 1, 0, {});
            setting_handlers[id] = [f = std::forward<Fn>(fn)](const std::string&) mutable { f(); };
        }
        void header(std::string id, std::string label) { define(abi::SettingKind::Header, std::move(id), std::move(label), "", {}, 0, 1, 0, {}); }
        void note(std::string id, std::string text) { define(abi::SettingKind::Label, std::move(id), std::move(text), "", {}, 0, 1, 0, {}); }
        // Runs after any setting of this mod changed (after the handle was updated).
        template <class Fn>
        void on_setting_changed(Fn&& fn) { setting_observers.push_back(std::forward<Fn>(fn)); }

        // Opens this mod's settings page.
        Result<void> open_settings() { return send(Intent(abi::HostAction::OpenModSettings)); }

        // ---- Intents ----
        // Handles an action sent by the game's UI or another mod. `description` appears in intent queries.
        template <class Fn>
        void on_intent(std::string action, std::string description, Fn&& fn) {
            abi::Writer w;
            w.write(abi::IntentFilter{action, std::move(description)});
            (void)detail::call_void(Op::IntentRegister, w);
            intent_handlers_[action] = std::forward<Fn>(fn);
        }
        template <meta::fixed_string Action, class Fn>
        void on_intent(std::string_view description, Fn&& fn) { on_intent(std::string(Action.view()), std::string(description), std::forward<Fn>(fn)); }

        // Sends an intent; the callback gets the outcome and any reply extras.
        template <class Fn>
        Result<uint32_t> send(const Intent& intent, Fn&& callback) {
            abi::Writer w;
            w.write(intent.raw());
            auto id = detail::call_as<uint32_t>(Op::IntentSend, w);
            if (id) pending_intents[*id] = std::forward<Fn>(callback);
            return id;
        }
        Result<void> send(const Intent& intent) {
            auto id = send(intent, [](const IntentResult&) {});
            if (!id) return unexpected(id.error());
            return {};
        }
        Result<void> send(abi::HostAction action) { return send(Intent(action)); }

        // File intents. The callback receives whether the game did it.
        template <class Fn>
        Result<uint32_t> copy_file(std::string from, std::string to, Fn&& done) {
            return send(Intent(abi::HostAction::CopyFile).put("from", from).put("to", to), [f = std::forward<Fn>(done)](const IntentResult& r) mutable { f(r.handled()); });
        }
        template <class Fn>
        Result<uint32_t> move_file(std::string from, std::string to, Fn&& done) {
            return send(Intent(abi::HostAction::MoveFile).put("from", from).put("to", to), [f = std::forward<Fn>(done)](const IntentResult& r) mutable { f(r.handled()); });
        }
        // Downloads an https file straight to disk ("mod:name.zip", "resourcepacks:pack.zip", ...). The callback gets the
        // HTTP status (the body is not kept) or the error.
        template <class Fn>
        Result<uint32_t> download(std::string url, std::string to, Fn&& done) {
            const uint32_t request = net::detail_net::counter()++;
            net::detail_net::pending()[request] = std::forward<Fn>(done);
            auto sent = send(Intent(abi::HostAction::DownloadFile).put("url", url).put("to", to).put("request", static_cast<int64_t>(request)),
                             [request](const IntentResult& r) {
                                 if (r.handled()) return; // the HttpResponse event completes it
                                 auto& pending = net::detail_net::pending();
                                 if (const auto it = pending.find(request); it != pending.end()) {
                                     auto cb = std::move(it->second);
                                     pending.erase(it);
                                     net::Response failed;
                                     failed.error = r.outcome == abi::IntentOutcome::Denied ? "permission denied" : "the game refused the download";
                                     if (cb) cb(failed);
                                 }
                             });
            if (!sent) net::detail_net::pending().erase(request);
            return sent;
        }

        Result<void> open_screen(Screen screen) { return send(Intent(abi::HostAction::OpenScreen).put("screen", screen)); }

        Config& config() {
            if (!cfg) cfg = std::make_unique<Config>();
            return *cfg;
        }

        uint64_t tick_count() const { return ticks; }
        double uptime() const { return elapsed; }

        void dispatch(abi::Event id, abi::Reader& r, uint32_t& flags);
        void run_unload();

        uint32_t subscriptions() const {
            // Replies and setting changes are always wanted: the game sends them without being asked.
            uint32_t mask = bit(abi::Event::HttpResponse) | bit(abi::Event::SettingChanged) | bit(abi::Event::IntentResult);
            for (const auto& entry : handlers)
                if (!entry.second.empty()) mask |= 1u << entry.first;
            if (!timers.empty()) mask |= bit(abi::Event::Tick);
            if (!layers.empty()) mask |= bit(abi::Event::Frame);
            if (!commands.empty()) mask |= bit(abi::Event::Command);
            if (!keys.empty()) mask |= bit(abi::Event::Key);
            if (!intent_handlers_.empty()) mask |= bit(abi::Event::Intent);
            return mask;
        }

    private:
        static constexpr uint32_t bit(abi::Event e) { return 1u << static_cast<uint32_t>(e); }

        struct Timer {
            TimerId id;
            double due, period;
            std::function<void()> fn;
            bool alive = true;
        };
        struct Layer {
            std::string name;
            int z;
            std::function<void(ui::Canvas&)> draw;
            std::vector<uint8_t> last;
        };
        using Handler = std::function<Flow(abi::Reader&)>;

        template <class P, class Fn>
        Handler wrap(Fn&& fn) {
            return [f = std::forward<Fn>(fn)](abi::Reader& r) mutable -> Flow {
                if constexpr (std::is_same_v<P, abi::EventNothing>) {
                    if constexpr (std::is_void_v<std::invoke_result_t<Fn&>>) {
                        f();
                        return Flow::pass;
                    } else {
                        return f();
                    }
                } else {
                    P payload = r.read<P>();
                    if constexpr (std::is_invocable_v<Fn&>) {
                        // Handlers that do not need the payload may take nothing.
                        if constexpr (std::is_void_v<std::invoke_result_t<Fn&>>) {
                            f();
                            return Flow::pass;
                        } else {
                            return f();
                        }
                    } else if constexpr (std::is_void_v<std::invoke_result_t<Fn&, const P&>>) {
                        f(payload);
                        return Flow::pass;
                    } else {
                        return f(payload);
                    }
                }
            };
        }

        template <class Fn>
        std::function<void(bool)> wrap_key(Fn&& fn) {
            if constexpr (std::is_invocable_v<Fn&, bool>)
                return std::forward<Fn>(fn);
            else
                return [f = std::forward<Fn>(fn)](bool down) mutable {
                    if (down) f(); // a key that takes no argument fires on press
                };
        }

        void add_handler(abi::Event id, Handler h) { handlers[static_cast<uint32_t>(id)].push_back(std::move(h)); }

        void add_command(const std::string& name, const std::string& usage, const std::string& description, std::function<void(const CommandCall&)> fn) {
            abi::Writer w;
            w.write(name, usage.empty() ? std::string("/") + name : "/" + name + usage, description);
            (void)detail::call_void(Op::CommandRegister, w);
            commands[name] = std::move(fn);
        }

        template <class Fn>
        TimerId add_timer(Duration delay, Duration period, Fn&& fn) {
            const TimerId id = static_cast<TimerId>(next_timer++);
            timers.push_back({id, elapsed + delay.count(), period.count(), std::forward<Fn>(fn), true});
            return id;
        }

        void define(abi::SettingKind kind, std::string id, std::string label, std::string value, const SettingOptions& options, double min, double max, double step,
                    std::vector<std::string> choices, std::string* normalized = nullptr) {
            abi::SettingDef def;
            def.id = std::move(id);
            def.kind = kind;
            def.label = std::move(label);
            def.description = options.description;
            def.icon = options.icon;
            def.value = value;
            def.fallback = value;
            def.min = min;
            def.max = max;
            def.step = step;
            def.choices = std::move(choices);
            def.unit = options.unit;
            def.restart = options.restart;
            abi::Writer w;
            w.write(def);
            auto reply = detail::call_as<std::string>(Op::SettingDefine, w);
            if (normalized && reply) *normalized = *reply;
        }

        template <class T>
        Setting<T> declare(abi::SettingKind kind, std::string id, std::string label, std::string initial, const SettingOptions& options, double min, double max,
                           double step, std::vector<std::string> choices) {
            std::string current = initial;
            define(kind, id, std::move(label), std::move(initial), options, min, max, step, std::move(choices), &current);
            auto state = std::make_shared<T>();
            assign(*state, current);
            setting_handlers[id] = [state](const std::string& value) { assign(*state, value); };
            return Setting<T>(state, id);
        }

        template <class T>
        static void assign(T& out, const std::string& text) {
            if constexpr (std::is_same_v<T, std::string>) {
                out = text;
            } else {
                T value{};
                if (detail_cmd::parse_value<T>(text, value)) out = value;
            }
        }

        void run_timers();
        void draw_layers(const abi::EventFrame& frame);
        static std::vector<std::string> split(const std::string& text);

        std::unordered_map<uint32_t, std::vector<Handler>> handlers;
        std::vector<std::function<void()>> unload_handlers;
        std::vector<Timer> timers;
        std::vector<Layer> layers;
        std::unordered_map<std::string, std::function<void(const CommandCall&)>> commands;
        std::unordered_map<std::string, std::function<void(bool)>> keys;
        std::unordered_map<std::string, std::function<void(const std::string&)>> setting_handlers;
        std::vector<std::function<void(const abi::EventSettingChanged&)>> setting_observers;
        std::unordered_map<std::string, std::function<void(const IntentCall&)>> intent_handlers_;
        std::unordered_map<uint32_t, std::function<void(const IntentResult&)>> pending_intents;
        std::unique_ptr<Config> cfg;
        uint32_t next_timer = 1;
        uint64_t ticks = 0;
        double elapsed = 0;
    };

    inline std::vector<std::string> Context::split(const std::string& text) {
        std::vector<std::string> out;
        size_t at = 0;
        while (at < text.size()) {
            while (at < text.size() && text[at] == ' ') at++;
            if (at >= text.size()) break;
            std::string token;
            if (text[at] == '"') {
                at++;
                while (at < text.size() && text[at] != '"') token += text[at++];
                at++;
            } else {
                while (at < text.size() && text[at] != ' ') token += text[at++];
            }
            out.push_back(std::move(token));
        }
        return out;
    }

    inline void Context::run_timers() {
        for (size_t i = 0; i < timers.size(); i++) {
            Timer& t = timers[i];
            if (!t.alive || t.due > elapsed) continue;
            auto fn = t.fn;
            if (t.period > 0) t.due += t.period;
            else t.alive = false;
            fn();
        }
        std::erase_if(timers, [](const Timer& t) { return !t.alive; });
    }

    inline void Context::draw_layers(const abi::EventFrame& frame) {
        for (Layer& layer : layers) {
            ui::Canvas canvas(frame.guiWidth, frame.guiHeight);
            layer.draw(canvas);
            const auto& data = canvas.commands().data;
            if (data == layer.last) continue;
            layer.last = data;
            if (data.empty()) (void)ui::clear(layer.name);
            else (void)ui::submit(layer.name, layer.z, canvas);
        }
    }

    inline void Context::dispatch(abi::Event id, abi::Reader& r, uint32_t& flags) {
        switch (id) {
        case abi::Event::Tick: {
            abi::Reader copy = r;
            const auto t = copy.read<abi::payload_t<abi::Event::Tick>>();
            ticks = t.tick;
            elapsed += t.dt;
            run_timers();
            break;
        }
        case abi::Event::Frame: {
            abi::Reader copy = r;
            draw_layers(copy.read<abi::payload_t<abi::Event::Frame>>());
            break;
        }
        case abi::Event::Command: {
            abi::Reader copy = r;
            const auto c = copy.read<abi::payload_t<abi::Event::Command>>();
            if (const auto it = commands.find(c.name); it != commands.end()) {
                CommandCall call{c.name, c.args, split(c.args)};
                it->second(call);
                flags |= abi::kEventCancel;
            }
            break;
        }
        case abi::Event::Key: {
            abi::Reader copy = r;
            const auto k = copy.read<abi::payload_t<abi::Event::Key>>();
            if (const auto it = keys.find(k.id); it != keys.end()) it->second(k.down);
            break;
        }
        case abi::Event::SettingChanged: {
            abi::Reader copy = r;
            const auto e = copy.read<abi::payload_t<abi::Event::SettingChanged>>();
            if (const auto it = setting_handlers.find(e.id); it != setting_handlers.end()) it->second(e.value);
            for (auto& observer : setting_observers) observer(e);
            break;
        }
        case abi::Event::Intent: {
            abi::Reader copy = r;
            auto e = copy.read<abi::payload_t<abi::Event::Intent>>();
            if (const auto it = intent_handlers_.find(e.intent.action); it != intent_handlers_.end()) {
                const IntentCall call(e.request, e.sender, std::move(e.intent));
                it->second(call);
            }
            break;
        }
        case abi::Event::IntentResult: {
            abi::Reader copy = r;
            auto e = copy.read<abi::payload_t<abi::Event::IntentResult>>();
            if (const auto it = pending_intents.find(e.request); it != pending_intents.end()) {
                auto cb = std::move(it->second);
                pending_intents.erase(it);
                if (cb) cb(IntentResult{e.outcome, std::move(e.handler), Extras(std::move(e.extras))});
            }
            break;
        }
        case abi::Event::HttpResponse: {
            abi::Reader copy = r;
            const auto e = copy.read<abi::payload_t<abi::Event::HttpResponse>>();
            auto& pending = net::detail_net::pending();
            if (const auto it = pending.find(e.id); it != pending.end()) {
                net::Response response;
                response.status = e.status;
                response.error = e.error;
                for (const auto& h : e.headers) response.headers.push_back({h.first, h.second});
                response.body = e.body;
                auto cb = std::move(it->second);
                pending.erase(it);
                if (cb) cb(response);
            }
            break;
        }
        default:
            break;
        }
        const auto it = handlers.find(static_cast<uint32_t>(id));
        if (it == handlers.end()) return;
        for (Handler& h : it->second) {
            abi::Reader copy = r;
            if (h(copy) == Flow::cancel) flags |= abi::kEventCancel;
        }
    }

    inline void Context::run_unload() {
        for (auto& fn : unload_handlers) fn();
        if (cfg && cfg->changed()) (void)cfg->save();
    }

    namespace detail {
        inline Context& context() {
            static Context ctx;
            return ctx;
        }
    }
}

void perlin_plugin_main(perlin::Context& ctx);

extern "C" {
__attribute__((export_name("perlin_alloc"))) inline void* perlin_alloc(uint32_t size) { return ::operator new(size ? size : 1); }
__attribute__((export_name("perlin_free"))) inline void perlin_free(void* p) { ::operator delete(p); }
__attribute__((export_name("perlin_subscriptions"))) inline uint32_t perlin_subscriptions() { return perlin::detail::context().subscriptions(); }
__attribute__((export_name("perlin_event"))) inline uint32_t perlin_event(uint32_t id, const uint8_t* data, uint32_t len) {
    perlin::Context& ctx = perlin::detail::context();
    perlin::abi::Reader r(data, len);
    uint32_t flags = 0;
    const auto event = static_cast<perlin::abi::Event>(id);
    if (event == perlin::abi::Event::Load) {
        perlin_plugin_main(ctx);
        return 0;
    }
    if (event == perlin::abi::Event::Unload) {
        ctx.dispatch(event, r, flags);
        ctx.run_unload();
        return 0;
    }
    ctx.dispatch(event, r, flags);
    return flags;
}
}

#define PERLIN_PLUGIN(ctx_name) void perlin_plugin_main(perlin::Context& ctx_name)
