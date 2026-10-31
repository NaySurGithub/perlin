#include "plugin/PluginHost.hpp"

#include "client/mods/WasmRuntime.hpp"
#include "core/Json.hpp"
#include "core/Log.hpp"
#include "util/FileIo.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <random>

namespace perlin::server {
    namespace fs = std::filesystem;
    using mods::WasmInstance;

    struct PluginHost::Plugin {
        Info info;
        std::vector<uint8_t> wasm;
        std::unique_ptr<WasmInstance> instance;
        uint64_t subscriptions = 0;
        bool hasAlloc = false, hasFree = false;
        std::set<std::string> commands;
    };

    namespace {
        constexpr uint32_t kWasiNoSys = 52, kWasiBadf = 8;

        uint64_t NowNs() {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count());
        }

        template <class T> bool Poke(WasmInstance& w, uint64_t at, T value) {
            uint8_t* p = w.Memory(at, sizeof(T));
            if (!p)
                return false;
            std::memcpy(p, &value, sizeof(T));
            return true;
        }

        // just enough wasi for a freestanding reactor: no files, no sockets, output goes to the log
        WasmInstance::HostFn WasiFunction(const std::string& name, const std::string& pluginName) {
            using Ctx = WasmInstance::HostContext;
            const auto status = [](uint32_t code) {
                return [code](Ctx&, const uint64_t*, uint32_t, uint64_t* r, uint32_t nr) {
                    if (nr)
                        r[0] = code;
                    return true;
                };
            };
            if (name == "args_sizes_get" || name == "environ_sizes_get")
                return [](Ctx& c, const uint64_t* a, uint32_t, uint64_t* r, uint32_t nr) {
                    Poke<uint32_t>(c.instance, a[0], 0);
                    Poke<uint32_t>(c.instance, a[1], 0);
                    if (nr)
                        r[0] = 0;
                    return true;
                };
            if (name == "args_get" || name == "environ_get")
                return status(0);
            if (name == "clock_time_get")
                return [](Ctx& c, const uint64_t* a, uint32_t, uint64_t* r, uint32_t nr) {
                    Poke<uint64_t>(c.instance, a[2], NowNs());
                    if (nr)
                        r[0] = 0;
                    return true;
                };
            if (name == "random_get")
                return [](Ctx& c, const uint64_t* a, uint32_t, uint64_t* r, uint32_t nr) {
                    if (uint8_t* p = c.instance.Memory(a[0], a[1])) {
                        static thread_local std::mt19937_64 rng{std::random_device{}()};
                        for (uint64_t i = 0; i < a[1]; i++)
                            p[i] = static_cast<uint8_t>(rng());
                    }
                    if (nr)
                        r[0] = 0;
                    return true;
                };
            if (name == "fd_write")
                return [pluginName](Ctx& c, const uint64_t* a, uint32_t, uint64_t* r, uint32_t nr) {
                    // iovec: u32 pointer, u32 length (wasm32 layout)
                    uint32_t total = 0;
                    std::string text;
                    for (uint32_t i = 0; i < a[2]; i++) {
                        const uint8_t* iov = c.instance.Memory(a[1] + i * 8ull, 8);
                        if (!iov)
                            break;
                        uint32_t ptr, len;
                        std::memcpy(&ptr, iov, 4);
                        std::memcpy(&len, iov + 4, 4);
                        if (const uint8_t* s = c.instance.Memory(ptr, len))
                            text.append(reinterpret_cast<const char*>(s), len);
                        total += len;
                    }
                    if (!text.empty()) {
                        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                            text.pop_back();
                        if (!text.empty())
                            LogInfo("[" + pluginName + "] " + text);
                    }
                    Poke<uint32_t>(c.instance, a[3], total);
                    if (nr)
                        r[0] = 0;
                    return true;
                };
            if (name == "proc_exit")
                return [](Ctx& c, const uint64_t*, uint32_t, uint64_t*, uint32_t) {
                    c.error = "plugin called exit";
                    return false;
                };
            if (name == "sched_yield" || name == "fd_close" || name == "fd_sync")
                return status(0);
            if (name.starts_with("fd_"))
                return status(kWasiBadf);
            return status(kWasiNoSys);
        }

        // bits 0-31 from perlin_subscriptions, 32-63 from perlin_subscriptions_hi
        uint64_t ReadSubscriptions(WasmInstance& w) {
            std::string ignored;
            uint64_t lo = 0, hi = 0;
            if (w.HasExport("perlin_subscriptions"))
                w.Call("perlin_subscriptions", {}, &lo, ignored);
            if (w.HasExport("perlin_subscriptions_hi"))
                w.Call("perlin_subscriptions_hi", {}, &hi, ignored);
            return (lo & 0xFFFFFFFFu) | (hi << 32);
        }

        // calls perlin_event on one plugin; false when it trapped
        bool CallEvent(WasmInstance& w, bool hasAlloc, bool hasFree, uint32_t id,
                       std::span<const uint8_t> payload, uint32_t& result, std::string& error) {
            uint64_t ptr = 0;
            const uint64_t len = payload.size();
            if (len) {
                if (!hasAlloc) {
                    error = "plugin has no perlin_alloc export";
                    return false;
                }
                if (!w.Call("perlin_alloc", {len}, &ptr, error))
                    return false;
                uint8_t* dst = ptr ? w.Memory(ptr, len) : nullptr;
                if (!dst) {
                    error = "perlin_alloc returned a bad pointer";
                    return false;
                }
                std::memcpy(dst, payload.data(), payload.size());
            }
            uint64_t rc = 0;
            if (!w.Call("perlin_event", {id, ptr, len}, &rc, error))
                return false;
            result = static_cast<uint32_t>(rc);
            std::string ignored;
            if (len && hasFree)
                w.Call("perlin_free", {ptr}, nullptr, ignored);
            return true;
        }
    }

    PluginHost::PluginHost(PluginApi& api) : api(api) {
        RegisterOps();
    }
    PluginHost::~PluginHost() {
        UnloadAll();
    }

    void PluginHost::RegisterOps() {
        using abi::Op;
        using abi::Status;
        using Ctx = CallContext;
        const auto found = [](bool ok) { return ok ? Status::Ok : Status::NotFound; };

        ops.Raw(Op::Log, [](Ctx& c, abi::Reader& in, abi::Writer&) {
            const int32_t level = in.get<int32_t>();
            const std::string text = in.str();
            if (!in.Ok())
                return Status::BadArguments;
            Log(static_cast<LogLevel>(std::clamp(level, 0, 3)), "[" + c.plugin.info.name + "] " + text);
            return Status::Ok;
        });
        ops.Handle<Op::Clock>([](Ctx&) {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count());
        });
        // name, usage, description: the SDK's command registration
        ops.Raw(Op::CommandRegister, [](Ctx& c, abi::Reader& in, abi::Writer&) {
            std::string name = in.str();
            in.str();
            const std::string description = in.str();
            if (!in.Ok() || name.empty())
                return Status::BadArguments;
            if (name.starts_with('/'))
                name.erase(0, 1);
            c.plugin.commands.insert(name);
            c.api.RegisterCommand(c.plugin.info.name, name, description);
            return Status::Ok;
        });
        ops.Raw(Op::CommandUnregister, [](Ctx& c, abi::Reader& in, abi::Writer&) {
            c.plugin.commands.erase(in.str());
            return in.Ok() ? Status::Ok : Status::BadArguments;
        });
        ops.Handle<Op::Broadcast>([](Ctx& c, const abi::TextMessage& m) { c.api.Broadcast(m.text); });
        ops.Handle<Op::PlayerMessage>(
            [found](Ctx& c, const abi::PlayerText& m) { return found(c.api.Message(m.player, m.text)); });
        ops.Handle<Op::Kick>(
            [found](Ctx& c, const abi::PlayerText& m) { return found(c.api.Kick(m.player, m.text)); });
        ops.Handle<Op::OnlinePlayers>([](Ctx& c) {
            abi::OnlinePlayerList list;
            for (const auto& p : c.api.Players())
                list.players.push_back({p.id, p.name, p.x, p.y, p.z});
            return list;
        });
        ops.Handle<Op::Teleport>([found](Ctx& c, const abi::ServerTeleport& t) {
            return found(c.api.Teleport(t.player, t.x, t.y, t.z));
        });
        ops.Handle<Op::SetBlock>(
            [](Ctx& c, const abi::BlockWrite& b) { return c.api.SetBlock(b.x, b.y, b.z, b.block); });
        ops.Handle<Op::BlockName>(
            [](Ctx& c, const abi::BlockCoord& at) -> abi::host::Result<abi::TextMessage> {
                abi::TextMessage name;
                if (const Status s = c.api.GetBlock(at.x, at.y, at.z, name.text); s != Status::Ok)
                    return std::unexpected(s);
                return name;
            });
        ops.Handle<Op::ServerInfo>([](Ctx& c) {
            const PluginApi::Status s = c.api.GetStatus();
            return abi::ServerStatus{s.name, s.players, s.maxPlayers, s.tick, s.backend};
        });
        ops.Handle<Op::GiveItem>(
            [](Ctx& c, const abi::GiveItemRequest& r) -> abi::host::Result<abi::GiveItemResult> {
                if (r.count == 0 || r.count > 4096)
                    return std::unexpected(Status::BadArguments);
                const auto left = c.api.GiveItem(r.player, r.item, static_cast<int>(r.count));
                if (!left)
                    return std::unexpected(left.error());
                return abi::GiveItemResult{static_cast<uint32_t>(*left)};
            });
    }

    void PluginHost::LoadDirectory(const std::string& dir) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec))
            return;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dir, ec))
            if (entry.is_regular_file() && entry.path().extension() == ".wasm")
                files.push_back(entry.path());
        std::ranges::sort(files);
        for (const fs::path& file : files) {
            std::string error;
            if (!LoadFile(file.string(), error))
                LogWarn("plugin " + file.filename().string() + " not loaded: " + error);
        }
    }

    bool PluginHost::LoadFile(const std::string& path, std::string& error) {
        auto plugin = std::make_unique<Plugin>();
        plugin->info.file = path;
        plugin->info.name = fs::path(path).stem().string();
        plugin->info.engine = mods::WasmEngineName();
        if (!file::ReadBytes(path, plugin->wasm)) {
            error = "cannot read the file";
            return false;
        }
        // optional name.json beside the module
        if (std::string text; file::Read(fs::path(path).replace_extension(".json"), text)) {
            json::Value manifest;
            if (json::Parse(text, manifest)) {
                if (std::string name = manifest["name"].String(); !name.empty())
                    plugin->info.name = std::move(name);
                plugin->info.version = manifest["version"].String();
                plugin->info.author = manifest["author"].String();
                plugin->info.description = manifest["description"].String();
            }
        }

        Plugin* raw = plugin.get();
        const std::string pluginName = plugin->info.name;
        raw->instance = std::make_unique<WasmInstance>();
        const auto resolver = [this, raw, pluginName](const std::string& module,
                                                      const std::string& name) -> WasmInstance::HostFn {
            if (module == "perlin" && name == "call")
                return [this, raw](WasmInstance::HostContext& c, const uint64_t* a, uint32_t n, uint64_t* r,
                                   uint32_t nr) {
                    if (n != 5 || nr != 1) {
                        c.error = "perlin.call has the wrong signature";
                        return false;
                    }
                    const uint8_t* in = c.instance.Memory(a[1], a[2]);
                    uint8_t* out = c.instance.Memory(a[3], a[4]);
                    if ((!in && a[2]) || (!out && a[4])) {
                        r[0] = static_cast<uint32_t>(abi::Status::BadArguments);
                        return true;
                    }
                    CallContext ctx{*raw, api};
                    abi::Writer reply;
                    // server plugins hold every permission: the operator chose to install them
                    const abi::Status status = ops.Call(ctx, static_cast<uint32_t>(a[0]),
                                                        {in, static_cast<size_t>(a[2])}, reply, ~0u);
                    int32_t rc = static_cast<int32_t>(status);
                    if (status == abi::Status::Ok) {
                        if (reply.Size() > a[4]) {
                            // the SDK grows its buffer and calls again
                            rc = -static_cast<int32_t>(abi::kNeedMoreBase + reply.Size());
                        } else {
                            if (reply.Size())
                                std::memcpy(out, reply.Data(), reply.Size());
                            rc = static_cast<int32_t>(reply.Size());
                        }
                    }
                    r[0] = static_cast<uint32_t>(rc);
                    return true;
                };
            if (module == "wasi_snapshot_preview1")
                return WasiFunction(name, pluginName);
            if (module == "env" && name == "emscripten_notify_memory_growth")
                return [](WasmInstance::HostContext&, const uint64_t*, uint32_t, uint64_t* r, uint32_t nr) {
                    if (nr)
                        r[0] = 0;
                    return true;
                };
            return {};
        };

        if (!raw->instance->Load(raw->wasm.data(), raw->wasm.size(), resolver, error))
            return false;
        if (!raw->instance->HasExport("perlin_event")) {
            error = "not a Perlin plugin (no perlin_event export)";
            return false;
        }
        raw->hasAlloc = raw->instance->HasExport("perlin_alloc");
        raw->hasFree = raw->instance->HasExport("perlin_free");
        std::string ignored;
        if (raw->instance->HasExport("_initialize"))
            raw->instance->Call("_initialize", {}, nullptr, ignored);
        raw->info.enabled = true;
        LogInfo("plugin " + raw->info.name + (raw->info.version.empty() ? "" : " " + raw->info.version) +
                " loaded (" + (raw->instance->Is64() ? "wasm64" : "wasm32") + " on " + raw->info.engine +
                ")");
        plugins.push_back(std::move(plugin));
        uint32_t result = 0;
        // the SDK boots the plugin class on Load; subscriptions are only known after that
        CallPlugin(*raw, abi::Event::Load, {}, result);
        raw->subscriptions = ReadSubscriptions(*raw->instance);
        return true;
    }

    void PluginHost::UnloadAll() {
        for (auto& p : plugins) {
            uint32_t result = 0;
            if (p->info.enabled)
                CallPlugin(*p, abi::Event::Unload, {}, result);
        }
        plugins.clear();
    }

    bool PluginHost::CallPlugin(Plugin& p, abi::Event event, std::span<const uint8_t> payload,
                                uint32_t& result) {
        std::string error;
        if (CallEvent(*p.instance, p.hasAlloc, p.hasFree, static_cast<uint32_t>(event), payload, result,
                      error))
            return true;
        p.info.enabled = false;
        p.info.error = error;
        LogError("plugin " + p.info.name + " disabled after an error: " + error);
        return false;
    }

    bool PluginHost::AnyoneListens(abi::Event event) const {
        const uint64_t bit = abi::host::SubscriptionBit(event);
        return std::ranges::any_of(
            plugins, [bit](const auto& p) { return p->info.enabled && (p->subscriptions & bit); });
    }

    bool PluginHost::DispatchEncoded(abi::Event event, std::span<const uint8_t> payload) {
        const uint64_t bit = abi::host::SubscriptionBit(event);
        for (auto& p : plugins) {
            if (!p->info.enabled || !(p->subscriptions & bit))
                continue;
            uint32_t result = 0;
            if (CallPlugin(*p, event, payload, result) && (result & abi::kEventCancel))
                return true;
        }
        return false;
    }

    bool PluginHost::DispatchCommand(const std::string& plugin, uint64_t player, const std::string& command,
                                     const std::string& args) {
        for (auto& p : plugins) {
            if (p->info.name != plugin || !p->info.enabled)
                continue;
            const abi::Writer w = abi::host::Encode<abi::Event::Command>({command, args, player});
            uint32_t result = 0;
            return CallPlugin(*p, abi::Event::Command, {w.Data(), w.Size()}, result);
        }
        return false;
    }

    std::vector<PluginHost::Info> PluginHost::List() const {
        std::vector<Info> out;
        out.reserve(plugins.size());
        for (const auto& p : plugins)
            out.push_back(p->info);
        return out;
    }
}
