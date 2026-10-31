#pragma once
#include <type_traits>

#include "perlin/runtime.hpp"

namespace perlin {
    // a plugin is a class: derive from Plugin, override what it needs and PERLIN_REGISTER it. Every member
    // named on<Event>() for an event in abi_table.h (onTick, onChatReceived, onPlayerJoin, ...) is subscribed
    // automatically, with or without the payload parameter; returning Flow::Cancel cancels a cancellable event.
    // The same class runs on the client and on the server: each host raises only its own events.
    class Plugin {
    public:
        virtual ~Plugin() = default;
        // runs before onEnable; commands registered here exist before any event arrives
        virtual void registerCommands(Context&) {}
        virtual void onEnable(Context&) {}
        virtual void onDisable(Context&) {}

        Context& ctx() const { return *ctx_; }

    private:
        template <class T> friend void detail_boot(Context&);
        Context* ctx_ = nullptr;
    };

    // the old name of the context handed to plugin classes
    using PluginCtx = Context;

    namespace detail_plugin {
        template <class T> T& instance() {
            static T value;
            return value;
        }

#define PERLIN_WIRE_Internal(Name)
#define PERLIN_WIRE_Method(Name)                                                                             \
    if constexpr (requires(T& t, const typename event::Name::Payload& p) { t.on##Name(p); })                 \
        ctx.on<event::Name>([&self](const typename event::Name::Payload& p) { return self.on##Name(p); });   \
    else if constexpr (requires(T& t) { t.on##Name(); })                                                     \
        ctx.on<event::Name>([&self]() { return self.on##Name(); });
#define PERLIN_X(Name, Id, Payload, Hosts, Wiring) PERLIN_WIRE_##Wiring(Name)

        template <class T> void wire_events(T& self, Context& ctx) {
            PERLIN_ABI_EVENTS(PERLIN_X)
        }

#undef PERLIN_X
#undef PERLIN_WIRE_Method
#undef PERLIN_WIRE_Internal
    }

    template <class T> void detail_boot(Context& ctx) {
        static_assert(std::is_base_of_v<Plugin, T>, "a plugin derives from perlin::Plugin");
        T& self = detail_plugin::instance<T>();
        static_cast<Plugin&>(self).ctx_ = &ctx;
        self.registerCommands(ctx);
        detail_plugin::wire_events(self, ctx);
        self.onEnable(ctx);
        ctx.on_unload([&self, &ctx] { self.onDisable(ctx); });
    }
}

#define PERLIN_REGISTER(T)                                                                                   \
    void perlin_plugin_main(::perlin::Context& c) {                                                          \
        ::perlin::detail_boot<T>(c);                                                                         \
    }
