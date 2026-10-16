#pragma once
#include <chrono>
#include <concepts>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <utility>
#include <vector>

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

    namespace event {
        using Nothing = std::monostate;
#define PERLIN_EVENT_TAG(Name, PayloadType) \
        struct Name { using Payload = PayloadType; static constexpr abi::Event id = abi::Event::Name; }
        PERLIN_EVENT_TAG(Tick, abi::EventTick);
        PERLIN_EVENT_TAG(Frame, abi::EventFrame);
        PERLIN_EVENT_TAG(ChatReceived, abi::EventChat);
        PERLIN_EVENT_TAG(ChatSending, abi::EventText);
        PERLIN_EVENT_TAG(Connected, abi::EventConnection);
        PERLIN_EVENT_TAG(Disconnected, abi::EventConnection);
        PERLIN_EVENT_TAG(Key, abi::EventKey);
        PERLIN_EVENT_TAG(Command, abi::EventCommand);
        PERLIN_EVENT_TAG(Damage, abi::EventDamage);
        PERLIN_EVENT_TAG(Death, abi::EventText);
        PERLIN_EVENT_TAG(Respawned, Nothing);
        PERLIN_EVENT_TAG(DimensionChanged, abi::EventChange);
        PERLIN_EVENT_TAG(ScreenChanged, abi::EventChange);
        PERLIN_EVENT_TAG(SlotChanged, abi::EventSlot);
        PERLIN_EVENT_TAG(EntityAdded, abi::EventEntity);
        PERLIN_EVENT_TAG(EntityRemoved, abi::EventEntity);
        PERLIN_EVENT_TAG(Sound, abi::EventSound);
        PERLIN_EVENT_TAG(HttpResponse, abi::EventHttp);
        PERLIN_EVENT_TAG(Packet, abi::EventPacket);
        PERLIN_EVENT_TAG(Title, abi::EventTitle);
#undef PERLIN_EVENT_TAG
    }

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

    enum class Key : int {
        None = 0,
        A = 'A', B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Num0 = '0', Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        Space = 0x20, Tab = 0x09, Enter = 0x0D, Shift = 0x10, Control = 0x11, Alt = 0x12,
        Left = 0x25, Up, Right, Down, Home = 0x24, End = 0x23, PageUp = 0x21, PageDown = 0x22, Delete = 0x2E,
        MouseMiddle = 1000, MouseLeft, MouseRight,
    };

    inline std::string key_id(Key key) {
        const int k = static_cast<int>(key);
        if ((k >= 'A' && k <= 'Z')) return std::string("key.keyboard.") + static_cast<char>(k - 'A' + 'a');
        if ((k >= '0' && k <= '9')) return std::string("key.keyboard.") + static_cast<char>(k);
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

    template <class T>
    concept EventTag = requires {
        typename T::Payload;
        T::id;
    };

    class Context {
    public:
        using Duration = std::chrono::duration<double>;

        template <EventTag Tag, class Fn>
        void on(Fn&& fn) {
            add_handler(Tag::id, wrap<typename Tag::Payload>(std::forward<Fn>(fn)));
        }

        template <class Fn>
        void on_chat(Fn&& fn) { on<event::ChatReceived>(std::forward<Fn>(fn)); }

        template <class Fn>
        void on_tick(Fn&& fn) { on<event::Tick>(std::forward<Fn>(fn)); }

        template <class Fn>
        void on_frame(Fn&& fn) { on<event::Frame>(std::forward<Fn>(fn)); }

        template <class Fn>
        void on_unload(Fn&& fn) { unload_handlers.push_back(std::forward<Fn>(fn)); }

        template <class Fn>
        uint32_t after(Duration delay, Fn&& fn) {
            return add_timer(delay, Duration::zero(), std::forward<Fn>(fn));
        }

        template <class Fn>
        uint32_t every(Duration period, Fn&& fn) {
            return add_timer(period, period, std::forward<Fn>(fn));
        }

        void cancel(uint32_t timer) {
            for (auto& t : timers)
                if (t.id == timer)
                    t.alive = false;
        }

        template <class Fn>
        void command(std::string name, std::string usage, std::string description, Fn&& fn) {
            abi::Writer w;
            w.str(name).str(usage).str(description);
            (void)detail::call_void(Op::CommandRegister, w);
            commands[name] = std::forward<Fn>(fn);
        }

        template <class Fn>
        void bind(std::string id, std::string label, Key default_key, Fn&& fn) {
            abi::Writer w;
            w.str(id).str(label).str(key_id(default_key));
            (void)detail::call_void(Op::KeyRegister, w);
            keys[id] = std::forward<Fn>(fn);
        }

        template <class Fn>
        void hud(std::string layer, Fn&& fn, int z = 0) {
            layers.push_back({std::move(layer), z, std::forward<Fn>(fn), {}});
        }

        template <class... Args>
        void print(std::format_string<Args...> fmt, Args&&... args) { (void)act::message(fmt, std::forward<Args>(args)...); }

        Config& config() {
            if (!cfg)
                cfg = std::make_unique<Config>();
            return *cfg;
        }

        uint64_t tick_count() const { return ticks; }
        double uptime() const { return elapsed; }

        void dispatch(abi::Event id, abi::Reader& r, uint32_t& flags);
        void run_unload();

        uint32_t subscriptions() const {
            uint32_t mask = 1u << static_cast<uint32_t>(abi::Event::HttpResponse);
            for (const auto& entry : handlers)
                if (!entry.second.empty())
                    mask |= 1u << entry.first;
            if (!timers.empty())
                mask |= 1u << static_cast<uint32_t>(abi::Event::Tick);
            if (!layers.empty())
                mask |= 1u << static_cast<uint32_t>(abi::Event::Frame);
            if (!commands.empty())
                mask |= 1u << static_cast<uint32_t>(abi::Event::Command);
            if (!keys.empty())
                mask |= 1u << static_cast<uint32_t>(abi::Event::Key);
            return mask;
        }

    private:
        struct Timer {
            uint32_t id;
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
                if constexpr (std::is_same_v<P, std::monostate>) {
                    if constexpr (std::is_void_v<std::invoke_result_t<Fn&>>) {
                        f();
                        return Flow::pass;
                    } else {
                        return f();
                    }
                } else {
                    P payload = P::Read(r);
                    if constexpr (std::is_void_v<std::invoke_result_t<Fn&, const P&>>) {
                        f(payload);
                        return Flow::pass;
                    } else {
                        return f(payload);
                    }
                }
            };
        }

        void add_handler(abi::Event id, Handler h) { handlers[static_cast<uint32_t>(id)].push_back(std::move(h)); }

        template <class Fn>
        uint32_t add_timer(Duration delay, Duration period, Fn&& fn) {
            const uint32_t id = next_timer++;
            timers.push_back({id, elapsed + delay.count(), period.count(), std::forward<Fn>(fn), true});
            return id;
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
        std::unique_ptr<Config> cfg;
        uint32_t next_timer = 1;
        uint64_t ticks = 0;
        double elapsed = 0;
    };

    inline std::vector<std::string> Context::split(const std::string& text) {
        std::vector<std::string> out;
        size_t at = 0;
        while (at < text.size()) {
            while (at < text.size() && text[at] == ' ')
                at++;
            if (at >= text.size())
                break;
            std::string token;
            if (text[at] == '"') {
                at++;
                while (at < text.size() && text[at] != '"')
                    token += text[at++];
                at++;
            } else {
                while (at < text.size() && text[at] != ' ')
                    token += text[at++];
            }
            out.push_back(std::move(token));
        }
        return out;
    }

    inline void Context::run_timers() {
        for (size_t i = 0; i < timers.size(); i++) {
            Timer& t = timers[i];
            if (!t.alive || t.due > elapsed)
                continue;
            auto fn = t.fn;
            if (t.period > 0)
                t.due += t.period;
            else
                t.alive = false;
            fn();
        }
        std::erase_if(timers, [](const Timer& t) { return !t.alive; });
    }

    inline void Context::draw_layers(const abi::EventFrame& frame) {
        for (Layer& layer : layers) {
            ui::Canvas canvas(frame.guiWidth, frame.guiHeight);
            layer.draw(canvas);
            const auto& data = canvas.commands().data;
            if (data == layer.last)
                continue;
            layer.last = data;
            if (data.empty())
                (void)ui::clear(layer.name);
            else
                (void)ui::submit(layer.name, layer.z, canvas);
        }
    }

    inline void Context::dispatch(abi::Event id, abi::Reader& r, uint32_t& flags) {
        if (id == abi::Event::Tick) {
            abi::Reader copy = r;
            const abi::EventTick t = abi::EventTick::Read(copy);
            ticks = t.tick;
            elapsed += t.dt;
            run_timers();
        } else if (id == abi::Event::Frame) {
            abi::Reader copy = r;
            const abi::EventFrame f = abi::EventFrame::Read(copy);
            draw_layers(f);
        } else if (id == abi::Event::Command) {
            abi::Reader copy = r;
            const abi::EventCommand c = abi::EventCommand::Read(copy);
            if (const auto it = commands.find(c.name); it != commands.end()) {
                CommandCall call{c.name, c.args, split(c.args)};
                it->second(call);
                flags |= abi::kEventCancel;
            }
        } else if (id == abi::Event::Key) {
            abi::Reader copy = r;
            const abi::EventKey k = abi::EventKey::Read(copy);
            if (const auto it = keys.find(k.id); it != keys.end())
                it->second(k.down);
        } else if (id == abi::Event::HttpResponse) {
            abi::Reader copy = r;
            const abi::EventHttp e = abi::EventHttp::Read(copy);
            auto& pending = net::detail_net::pending();
            if (const auto it = pending.find(e.id); it != pending.end()) {
                net::Response response;
                response.status = e.status;
                response.error = e.error;
                for (const auto& h : e.headers)
                    response.headers.push_back({h.first, h.second});
                response.body = e.body;
                auto cb = std::move(it->second);
                pending.erase(it);
                if (cb)
                    cb(response);
            }
        }
        const auto it = handlers.find(static_cast<uint32_t>(id));
        if (it == handlers.end())
            return;
        for (Handler& h : it->second) {
            abi::Reader copy = r;
            if (h(copy) == Flow::cancel)
                flags |= abi::kEventCancel;
        }
    }

    inline void Context::run_unload() {
        for (auto& fn : unload_handlers)
            fn();
        if (cfg && cfg->changed())
            (void)cfg->save();
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
