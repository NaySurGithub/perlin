#include "client/mods/ModManager.hpp"

#include "client/mods/WasmRuntime.hpp"

#include "client/ui/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <set>

namespace perlin::mods {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    namespace {
        thread_local const void* tlsPlugin = nullptr;
        const char* const kRevoked = "plugin revoked";
        const char* const kExit = "plugin exited";

        int64_t UnixMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        const char* EventName(uint32_t id) {
            switch (static_cast<abi::Event>(id)) {
            case abi::Event::Tick: return "tick";
            case abi::Event::Frame: return "frame";
            case abi::Event::ChatReceived: return "chat";
            case abi::Event::ChatSending: return "chat_sending";
            case abi::Event::Connected: return "connected";
            case abi::Event::Disconnected: return "disconnected";
            case abi::Event::Key: return "key";
            case abi::Event::Command: return "command";
            case abi::Event::Damage: return "damage";
            case abi::Event::Death: return "death";
            case abi::Event::Respawned: return "respawned";
            case abi::Event::DimensionChanged: return "dimension";
            case abi::Event::ScreenChanged: return "screen";
            case abi::Event::SlotChanged: return "slot";
            case abi::Event::EntityAdded: return "entity_added";
            case abi::Event::EntityRemoved: return "entity_removed";
            case abi::Event::Sound: return "sound";
            case abi::Event::HttpResponse: return "http";
            case abi::Event::Packet: return "packet";
            case abi::Event::Title: return "title";
            case abi::Event::SettingChanged: return "setting_changed";
            case abi::Event::Intent: return "intent";
            case abi::Event::IntentResult: return "intent_result";
            default: return nullptr;
            }
        }

        bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            out.assign(std::istreambuf_iterator<char>(in), {});
            return !out.empty();
        }

        bool SafeRelative(const std::string& rel, fs::path& out) {
            if (rel.empty() || rel.size() > 240 || rel.find('\\') != std::string::npos || rel.find('\0') != std::string::npos)
                return false;
            const fs::path path = fs::path(rel).lexically_normal();
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
                return false;
            for (const auto& part : path) {
                const std::string s = part.string();
                if (s == ".." || s.empty())
                    return false;
            }
            out = path;
            return true;
        }

        std::string Escape(const std::string& s) {
            std::string out;
            for (const char c : s) {
                if (c == '"' || c == '\\')
                    out += '\\';
                if (static_cast<unsigned char>(c) < 0x20)
                    continue;
                out += c;
            }
            return out;
        }
    }

    struct ModManager::Plugin {
        enum class Kind { None, Load, Event, Quit };

        std::string name, path, storage;
        fs::file_time_type stamp = fs::file_time_type::min();
        uintmax_t size = 0;
        bool enabled = true;
        std::string error;
        int loads = 0;
        ModInfo meta;
        uint32_t permissions = static_cast<uint32_t>(abi::Permission::Storage);
        uint32_t requested = 0, safePermissions = static_cast<uint32_t>(abi::Permission::Storage);
        bool needsApproval = false;
        std::vector<PluginCommand> commands;
        std::vector<PluginKey> keys;
        std::map<std::string, UiLayer> layers;
        std::map<std::string, std::string> savedKeys;
        std::vector<abi::SettingDef> settings;
        std::map<std::string, std::string> savedSettings;
        bool settingsDirty = false, restartPending = false;
        std::vector<abi::IntentFilter> intentFilters;
        std::string runtimeIcon;
        uint32_t replyingTo = 0;
        bool replied = false;
        std::vector<abi::Extra> replyExtras;
        std::atomic<bool> loaded{false};
        std::atomic<bool> stuck{false};
        std::atomic<bool> revoked{false};
        std::atomic<uint32_t> subs{0xFFFFFFFFu};
        std::atomic<double> lastEventMs{0};
        uint64_t serial = 0;

        std::thread thread;
        std::mutex jobMutex;
        std::condition_variable jobCv, doneCv;
        Kind kind = Kind::None;
        abi::Event event = abi::Event::Tick;
        std::vector<uint8_t> payload;
        std::vector<uint8_t> wasm;
        bool done = false;
        uint32_t flags = 0;
        std::string jobError;
        std::mutex gate;

        std::unique_ptr<WasmInstance> instance;
        std::string arch;
        bool hasAlloc = false, hasFree = false, hasSubs = false;
    };

    struct HostBridge {
        static ModManager* manager;

        // perlin.call(op, in, in_len, out, out_cap) -> i32. Pointers and lengths may be i32 or
        // i64 (32-bit or 64-bit plugins); the result is a byte count or a negative status.
        static bool Call(ModManager::Plugin& plugin, WasmInstance::HostContext& ctx, const uint64_t* a, uint32_t n, uint64_t* r) {
            if (plugin.revoked) {
                ctx.error = kRevoked;
                return false;
            }
            if (n != 5) {
                ctx.error = "perlin.call has the wrong signature";
                return false;
            }
            const uint32_t op = static_cast<uint32_t>(a[0]);
            uint8_t* in = ctx.instance.Memory(a[1], a[2]);
            uint8_t* out = ctx.instance.Memory(a[3], a[4]);
            if (!in || !out || a[2] > 0xFFFFFFFFu || a[4] > 0xFFFFFFFFu) {
                r[0] = static_cast<uint32_t>(static_cast<int32_t>(abi::Status::BadArguments));
                return true;
            }
            const int32_t rc = manager->HostCall(plugin, op, in, static_cast<uint32_t>(a[2]), out, static_cast<uint32_t>(a[4]));
            if (rc == INT32_MIN) {
                ctx.error = kRevoked;
                return false;
            }
            r[0] = static_cast<uint32_t>(rc);
            return true;
        }
    };
    ModManager* HostBridge::manager = nullptr;

    namespace {
        // The slice of WASI preview 1 a plugin's libc needs: stdout/stderr go to the log, clocks
        // and randomness work, everything else (files, sockets) is unavailable. Pointers and
        // sizes are i32 in 32-bit plugins and i64 in 64-bit ones.
        constexpr uint32_t kBadf = 8, kFault = 21, kNosys = 52, kNotcapable = 76, kSpipe = 70;

        using Wasi = WasmInstance::HostFn;

        bool PutSize(WasmInstance& w, uint64_t at, uint64_t value) {
            const uint64_t width = w.Is64() ? 8 : 4;
            uint8_t* p = w.Memory(at, width);
            if (!p)
                return false;
            std::memcpy(p, &value, width); // little-endian
            return true;
        }

        Wasi Const(uint32_t code) {
            return [code](WasmInstance::HostContext&, const uint64_t*, uint32_t, uint64_t* r, uint32_t nr) {
                if (nr)
                    r[0] = code;
                return true;
            };
        }

        Wasi WasiFunction(ModManager::Plugin& plugin, const std::string& name) {
            using Ctx = WasmInstance::HostContext;
            if (name == "args_sizes_get" || name == "environ_sizes_get")
                return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                    r[0] = n >= 2 && PutSize(c.instance, a[0], 0) && PutSize(c.instance, a[1], 0) ? 0 : kFault;
                    return true;
                };
            if (name == "args_get" || name == "environ_get" || name == "fd_close" || name == "fd_sync" || name == "fd_datasync" || name == "sched_yield")
                return Const(0);
            if (name == "clock_time_get")
                return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                    uint8_t* out = n >= 3 ? c.instance.Memory(a[2], 8) : nullptr;
                    if (!out) {
                        r[0] = kFault;
                        return true;
                    }
                    const uint64_t ns = a[0] == 0
                                            ? static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count())
                                            : static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
                    std::memcpy(out, &ns, 8);
                    r[0] = 0;
                    return true;
                };
            if (name == "clock_res_get")
                return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                    uint8_t* out = n >= 2 ? c.instance.Memory(a[1], 8) : nullptr;
                    const uint64_t res = 1000;
                    if (out)
                        std::memcpy(out, &res, 8);
                    r[0] = out ? 0 : kFault;
                    return true;
                };
            if (name == "random_get")
                return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                    uint8_t* bytes = n >= 2 ? c.instance.Memory(a[0], a[1]) : nullptr;
                    if (!bytes) {
                        r[0] = kFault;
                        return true;
                    }
                    thread_local std::mt19937_64 rng{std::random_device{}()};
                    for (uint64_t i = 0; i < a[1]; i++)
                        bytes[i] = static_cast<uint8_t>(rng());
                    r[0] = 0;
                    return true;
                };
            if (name == "fd_write")
                return [&plugin](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                    if (n < 4)
                        return false;
                    if (a[0] != 1 && a[0] != 2) {
                        r[0] = kBadf;
                        return true;
                    }
                    const uint64_t stride = c.instance.Is64() ? 16 : 8, half = stride / 2;
                    const uint64_t count = a[2];
                    const uint8_t* iovs = count <= 1024 ? c.instance.Memory(a[1], count * stride) : nullptr;
                    if (!iovs) {
                        r[0] = kFault;
                        return true;
                    }
                    // Copy the iovec list first: the buffers it points at are looked up separately.
                    std::vector<uint8_t> list(iovs, iovs + count * stride);
                    std::string text;
                    uint64_t total = 0;
                    for (uint64_t i = 0; i < count; i++) {
                        uint64_t ptr = 0, len = 0;
                        std::memcpy(&ptr, list.data() + i * stride, half);
                        std::memcpy(&len, list.data() + i * stride + half, half);
                        const uint8_t* buf = c.instance.Memory(ptr, len);
                        if (!buf) {
                            r[0] = kFault;
                            return true;
                        }
                        if (text.size() < 65536)
                            text.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(std::min<uint64_t>(len, 65536)));
                        total += len;
                    }
                    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                        text.pop_back();
                    if (!text.empty())
                        std::cout << "[mod:" << plugin.name << "] " << text << std::endl;
                    r[0] = PutSize(c.instance, a[3], total) ? 0 : kFault;
                    return true;
                };
            if (name == "fd_fdstat_get")
                return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                    uint8_t* out = n >= 2 && a[0] <= 2 ? c.instance.Memory(a[1], 24) : nullptr;
                    if (!out) {
                        r[0] = kBadf;
                        return true;
                    }
                    uint8_t stat[24] = {};
                    stat[0] = 2; // character device
                    const uint64_t rights = a[0] == 0 ? (1ull << 1) : (1ull << 6);
                    std::memcpy(stat + 8, &rights, 8);
                    std::memcpy(out, stat, 24);
                    r[0] = 0;
                    return true;
                };
            if (name == "proc_exit")
                return [](Ctx& c, const uint64_t*, uint32_t, uint64_t*, uint32_t) {
                    c.error = kExit;
                    return false;
                };
            if (name == "fd_seek" || name == "fd_tell")
                return Const(kSpipe);
            if (name.rfind("fd_", 0) == 0)
                return Const(kBadf);
            if (name.rfind("path_", 0) == 0)
                return Const(kNotcapable);
            // poll_oneoff, proc_raise, sock_* and anything newer.
            return Const(kNosys);
        }

        WasmInstance::Resolver PluginImports(ModManager::Plugin& plugin) {
            return [&plugin](const std::string& module, const std::string& name) -> WasmInstance::HostFn {
                if (module == "perlin" && name == "call")
                    return [&plugin](WasmInstance::HostContext& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t nr) {
                        if (nr != 1) {
                            c.error = "perlin.call must return i32";
                            return false;
                        }
                        return HostBridge::Call(plugin, c, a, n, r);
                    };
                if (module == "wasi_snapshot_preview1")
                    return WasiFunction(plugin, name);
                // Emscripten's standalone output reports memory growth; nothing to do.
                if (module == "env" && name == "emscripten_notify_memory_growth")
                    return Const(0);
                return {};
            };
        }
    }

    ModManager& ModManager::Instance() {
        static ModManager* instance = new ModManager();
        return *instance;
    }

    ModManager::~ModManager() { Stop(); }

    void ModManager::SetEnvironment(const std::string& version, const std::string& platformName) {
        std::lock_guard lock(mutex);
        gameVersion = version;
        platform = platformName;
    }

    void ModManager::Register(abi::Op op, OpHandler handler, uint32_t permission) {
        ops[static_cast<uint32_t>(op)] = {std::move(handler), permission};
    }

    void ModManager::SetDisabled(const std::vector<std::string>& names) {
        std::lock_guard lock(mutex);
        disabled = names;
        lastScan = {};
    }

    // Caller holds the mutex. A mod only ever runs with what was approved, so a mod that later asks for more cannot use it unseen.
    void ModManager::ApplyApproval(Plugin& mod) {
        const auto granted = approved.find(mod.name);
        const uint32_t allowed = granted == approved.end() ? 0u : granted->second;
        mod.needsApproval = (mod.requested & ~allowed) != 0;
        mod.permissions = mod.safePermissions | (allowed & mod.requested);
        mod.meta.requested = mod.requested;
        mod.meta.needsApproval = mod.needsApproval;
    }

    uint32_t ModManager::DangerousMask() {
        return static_cast<uint32_t>(abi::Permission::Network) | static_cast<uint32_t>(abi::Permission::Packets) |
               static_cast<uint32_t>(abi::Permission::Control) | static_cast<uint32_t>(abi::Permission::Files);
    }

    void ModManager::SetApproved(const std::vector<std::string>& entries) {
        std::lock_guard lock(mutex);
        approved.clear();
        for (const std::string& e : entries) {
            const size_t eq = e.rfind('=');
            if (eq == std::string::npos)
                continue;
            approved[e.substr(0, eq)] = static_cast<uint32_t>(std::strtoul(e.c_str() + eq + 1, nullptr, 10));
        }
        lastScan = {};
    }

    void ModManager::ReloadAll() {
        std::lock_guard lock(mutex);
        reloadAll = true;
        lastScan = {};
    }

    void ModManager::ReloadMod(const std::string& name) {
        std::lock_guard lock(mutex);
        reloadNames.push_back(name);
        lastScan = {};
    }

    void ModManager::Start(const std::string& dir) {
        HostBridge::manager = this;
        RegisterCore();
        directory = dir;
        std::error_code ec;
        fs::create_directories(directory, ec);
        running = true;
        lastScan = {};
    }

    void ModManager::Stop() {
        if (!running.exchange(false) && mods.empty())
            return;
        std::vector<std::unique_ptr<Plugin>> taken;
        {
            std::lock_guard lock(mutex);
            taken.swap(mods);
        }
        for (auto& mod : taken)
            Unload(*mod);
        for (auto& mod : taken)
            if (mod->stuck)
                graveyard.push_back(std::move(mod));
        layerVersion++;
    }

    bool ModManager::HasPlugins() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        for (const auto& mod : mods)
            if (mod->loaded)
                return true;
        return false;
    }

    bool ModManager::HasPermission(const std::string& plugin, abi::Permission p) const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        for (const auto& mod : mods)
            if (mod->name == plugin)
                return (mod->permissions & static_cast<uint32_t>(p)) != 0;
        return false;
    }

    ModManager::Plugin* ModManager::Find(const std::string& name) const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        for (const auto& mod : mods)
            if (mod->name == name)
                return mod.get();
        return nullptr;
    }

    std::vector<ModInfo> ModManager::List() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<ModInfo> out;
        for (const auto& mod : mods) {
            ModInfo info = mod->meta;
            info.name = mod->name;
            info.enabled = mod->enabled;
            info.loads = mod->loads;
            info.error = mod->error;
            info.lastEventMs = mod->lastEventMs.load();
            info.arch = mod->arch;
            info.hasSettings = mod->loaded && !mod->settings.empty();
            if (!mod->runtimeIcon.empty())
                info.iconPath = mod->runtimeIcon;
            info.state = mod->needsApproval ? "approval" : !mod->enabled ? "disabled" : mod->stuck ? "unresponsive" : !mod->error.empty() ? "error" : mod->loaded ? "running" : "loading";
            info.hooks.clear();
            if (mod->loaded) {
                const uint32_t subs = mod->subs.load();
                for (uint32_t id = 1; id < 32; id++)
                    if ((subs & (1u << id)) && id != static_cast<uint32_t>(abi::Event::HttpResponse))
                        if (const char* n = EventName(id))
                            info.hooks.push_back(n);
            }
            out.push_back(std::move(info));
        }
        std::sort(out.begin(), out.end(), [](const ModInfo& a, const ModInfo& b) { return a.name < b.name; });
        return out;
    }

    std::vector<PluginCommand> ModManager::Commands() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<PluginCommand> out;
        for (const auto& mod : mods)
            if (mod->loaded)
                out.insert(out.end(), mod->commands.begin(), mod->commands.end());
        return out;
    }

    std::vector<PluginKey> ModManager::Keys() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<PluginKey> out;
        for (const auto& mod : mods)
            if (mod->loaded)
                out.insert(out.end(), mod->keys.begin(), mod->keys.end());
        return out;
    }

    std::vector<UiLayer> ModManager::Layers() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<UiLayer> out;
        for (const auto& mod : mods)
            if (mod->loaded)
                for (const auto& entry : mod->layers)
                    out.push_back(entry.second);
        std::stable_sort(out.begin(), out.end(), [](const UiLayer& a, const UiLayer& b) { return a.z < b.z; });
        return out;
    }

    std::string ModManager::KeysFile(const Plugin& mod) const { return (fs::path(directory) / (mod.name + ".keys.json")).string(); }

    void ModManager::LoadKeys(Plugin& mod) {
        mod.savedKeys.clear();
        std::ifstream in(KeysFile(mod), std::ios::binary);
        if (!in)
            return;
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        ui::Json json;
        std::string error;
        if (!ui::Json::Parse(text, json, error) || !json.IsObject())
            return;
        for (const auto& entry : json.object)
            if (entry.second.IsString())
                mod.savedKeys[entry.first] = entry.second.string;
    }

    void ModManager::SaveKeys(const Plugin& mod) const {
        std::string text = "{\n";
        bool first = true;
        for (const auto& entry : mod.savedKeys) {
            if (!first)
                text += ",\n";
            first = false;
            text += "  \"" + Escape(entry.first) + "\": \"" + Escape(entry.second) + "\"";
        }
        text += "\n}\n";
        std::ofstream out(KeysFile(mod), std::ios::binary | std::ios::trunc);
        out << text;
    }

    void ModManager::SetKeyBinding(const std::string& plugin, const std::string& id, const std::string& binding) {
        std::lock_guard lock(mutex);
        for (auto& mod : mods) {
            if (mod->name != plugin)
                continue;
            for (auto& key : mod->keys)
                if (key.id == id)
                    key.binding = binding;
            mod->savedKeys[id] = binding;
            SaveKeys(*mod);
        }
    }

    int32_t ModManager::HostCall(Plugin& mod, uint32_t op, const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t outCap) {
        std::lock_guard gateLock(mod.gate);
        if (mod.revoked)
            return INT32_MIN;
        const auto it = ops.find(op);
        if (it == ops.end())
            return static_cast<int32_t>(abi::Status::UnknownOp);
        if (it->second.second && !(mod.permissions & it->second.second))
            return static_cast<int32_t>(abi::Status::Denied);
        CallContext ctx{mod.name, mod.permissions, mod.storage, &mod};
        abi::Reader reader(in, inLen);
        abi::Writer writer;
        const abi::Status status = it->second.first(ctx, reader, writer);
        if (status != abi::Status::Ok)
            return static_cast<int32_t>(status);
        if (writer.Size() > outCap)
            return -(abi::kNeedMoreBase + static_cast<int32_t>(std::min<size_t>(writer.Size(), 1u << 28)));
        if (writer.Size())
            std::memcpy(out, writer.Data(), writer.Size());
        return static_cast<int32_t>(writer.Size());
    }

    void ModManager::Revoke(Plugin& mod, const char* why) {
        {
            std::lock_guard gateLock(mod.gate);
            mod.revoked = true;
            // Stop whatever it is running; its worker then finishes and can be joined.
            if (mod.instance)
                mod.instance->Interrupt();
        }
        mod.stuck = true;
        mod.loaded = false;
        {
            std::lock_guard lock(mutex);
            mod.error = why;
            mod.commands.clear();
            mod.keys.clear();
            mod.layers.clear();
            mod.settings.clear();
            mod.intentFilters.clear();
            mod.runtimeIcon.clear();
        }
        layerVersion++;
        commandVersion++;
        std::cout << "[mods] " << mod.name << ": " << why << std::endl;
    }

    void ModManager::Spawn(Plugin& mod) {
        mod.kind = Plugin::Kind::None;
        mod.thread = std::thread(&ModManager::Worker, this, &mod);
    }

    void ModManager::Worker(ModManager* self, Plugin* mod) {
        tlsPlugin = mod;
        for (;;) {
            std::unique_lock lock(mod->jobMutex);
            mod->jobCv.wait(lock, [&] { return mod->kind != Plugin::Kind::None; });
            const Plugin::Kind kind = mod->kind;
            mod->kind = Plugin::Kind::None;
            if (kind == Plugin::Kind::Quit)
                break;
            lock.unlock();
            if (kind == Plugin::Kind::Load)
                self->WorkerLoad(*mod);
            else
                self->WorkerEvent(*mod);
            lock.lock();
            mod->done = true;
            mod->doneCv.notify_all();
        }
        self->WorkerFree(*mod);
    }

    void ModManager::WorkerFree(Plugin& mod) {
        std::unique_ptr<WasmInstance> dead;
        {
            std::lock_guard gateLock(mod.gate);
            dead = std::move(mod.instance);
        }
        dead.reset();
        mod.hasAlloc = mod.hasFree = mod.hasSubs = false;
    }

    namespace {
        // Copies the payload into the plugin (perlin_alloc), runs perlin_event and frees it.
        bool CallEventFn(ModManager::Plugin& mod, uint32_t id, const std::vector<uint8_t>& payload, uint32_t& flags, std::string& error) {
            WasmInstance& w = *mod.instance;
            uint64_t ptr = 0;
            const uint64_t len = payload.size();
            if (len && !mod.hasAlloc) {
                error = "plugin has no perlin_alloc export";
                return false;
            }
            if (len) {
                if (!w.Call("perlin_alloc", {len}, &ptr, error)) {
                    error = "alloc: " + error;
                    return false;
                }
                uint8_t* dst = ptr ? w.Memory(ptr, len) : nullptr;
                if (!dst) {
                    error = "alloc returned a bad pointer";
                    return false;
                }
                std::memcpy(dst, payload.data(), payload.size());
            }
            uint64_t result = 0;
            if (!w.Call("perlin_event", {id, ptr, len}, &result, error)) {
                error = std::string(EventName(id) ? EventName(id) : "event") + ": " + error;
                return false;
            }
            flags = static_cast<uint32_t>(result);
            std::string ignored;
            if (len && mod.hasFree)
                w.Call("perlin_free", {ptr}, nullptr, ignored);
            return true;
        }

        void ReadSubscriptions(ModManager::Plugin& mod) {
            uint64_t subs = 0;
            std::string ignored;
            if (mod.instance->Call("perlin_subscriptions", {}, &subs, ignored))
                mod.subs = static_cast<uint32_t>(subs);
        }
    }

    void ModManager::WorkerLoad(Plugin& mod) {
        mod.jobError.clear();
        {
            std::lock_guard gateLock(mod.gate);
            mod.instance = std::make_unique<WasmInstance>();
            if (mod.revoked)
                mod.instance->Interrupt();
        }
        std::string error;
        if (!mod.instance->Load(mod.wasm.data(), mod.wasm.size(), PluginImports(mod), error)) {
            mod.jobError = error;
            return;
        }
        if (!mod.instance->HasExport("perlin_event")) {
            mod.jobError = "not a Perlin plugin (missing perlin_event export)";
            return;
        }
        mod.hasAlloc = mod.instance->HasExport("perlin_alloc");
        mod.hasFree = mod.instance->HasExport("perlin_free");
        mod.hasSubs = mod.instance->HasExport("perlin_subscriptions");
        {
            std::lock_guard lock(mutex);
            mod.arch = mod.instance->Is64() ? "wasm64" : "wasm32";
        }
        std::cout << "[mods] " << mod.name << ": " << mod.arch << " on " << WasmEngineName() << std::endl;
        // WASI reactors (and Emscripten's standalone output) set up their runtime here.
        if (mod.instance->HasExport("_initialize") && !mod.instance->Call("_initialize", {}, nullptr, error)) {
            mod.jobError = "_initialize: " + error;
            return;
        }
        uint32_t flags = 0;
        if (!CallEventFn(mod, static_cast<uint32_t>(abi::Event::Load), {}, flags, error)) {
            mod.jobError = error;
            return;
        }
        if (mod.hasSubs)
            ReadSubscriptions(mod);
    }

    void ModManager::WorkerEvent(Plugin& mod) {
        mod.jobError.clear();
        uint32_t flags = 0;
        std::string error;
        if (!CallEventFn(mod, static_cast<uint32_t>(mod.event), mod.payload, flags, error)) {
            mod.jobError = error;
            return;
        }
        mod.flags = flags;
        if (mod.hasSubs && mod.event != abi::Event::Frame && mod.event != abi::Event::Unload)
            ReadSubscriptions(mod);
    }

    bool ModManager::Call(Plugin& mod, abi::Event event, const abi::Writer& payload, double budgetMs, bool& cancelled) {
        if (!mod.loaded || mod.stuck || tlsPlugin)
            return false;
        const auto started = Clock::now();
        {
            std::lock_guard lock(mod.jobMutex);
            mod.event = event;
            mod.payload = payload.data;
            mod.done = false;
            mod.flags = 0;
            mod.kind = Plugin::Kind::Event;
        }
        mod.jobCv.notify_all();
        std::unique_lock lock(mod.jobMutex);
        const bool finished = mod.doneCv.wait_for(lock, std::chrono::duration<double, std::milli>(budgetMs), [&] { return mod.done; });
        if (!finished) {
            lock.unlock();
            Revoke(mod, "unresponsive: an event ran too long, the plugin was stopped");
            return false;
        }
        const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        mod.lastEventMs = elapsed;
        if (!mod.jobError.empty()) {
            const std::string message = mod.jobError;
            lock.unlock();
            std::lock_guard guard(mutex);
            mod.error = message;
            mod.loaded = false;
            mod.commands.clear();
            mod.keys.clear();
            mod.layers.clear();
            mod.settings.clear();
            mod.intentFilters.clear();
            mod.runtimeIcon.clear();
            layerVersion++;
            commandVersion++;
            std::cout << "[mods] " << mod.name << " stopped: " << message << std::endl;
            return false;
        }
        cancelled = (mod.flags & abi::kEventCancel) != 0;
        return true;
    }

    bool ModManager::WantsEvent(abi::Event event) const {
        std::vector<Plugin*> list;
        {
            std::lock_guard lock(const_cast<std::mutex&>(mutex));
            for (const auto& mod : mods)
                if (mod->loaded && !mod->stuck)
                    list.push_back(mod.get());
        }
        const uint32_t bit = 1u << static_cast<uint32_t>(event);
        for (Plugin* mod : list)
            if (mod->subs.load() & bit)
                return true;
        return false;
    }

    bool ModManager::Dispatch(abi::Event event, const abi::Writer& payload, double budgetMs) {
        if (tlsPlugin)
            return false;
        std::vector<Plugin*> list;
        {
            std::lock_guard lock(mutex);
            for (const auto& mod : mods)
                if (mod->loaded && !mod->stuck)
                    list.push_back(mod.get());
        }
        const uint32_t bit = 1u << static_cast<uint32_t>(event);
        bool cancelled = false;
        for (Plugin* mod : list) {
            if (!(mod->subs.load() & bit))
                continue;
            bool c = false;
            if (Call(*mod, event, payload, budgetMs, c) && c)
                cancelled = true;
        }
        return cancelled;
    }

    void ModManager::DispatchTo(const std::string& plugin, abi::Event event, const abi::Writer& payload, double budgetMs) {
        if (tlsPlugin)
            return;
        Plugin* mod = Find(plugin);
        if (!mod)
            return;
        bool c = false;
        Call(*mod, event, payload, budgetMs, c);
    }

    void ModManager::Unload(Plugin& mod) {
        if (mod.loaded && !mod.stuck) {
            bool c = false;
            Call(mod, abi::Event::Unload, abi::Writer{}, 500.0, c);
        }
        SaveSettings(mod);
        mod.loaded = false;
        if (mod.thread.joinable()) {
            if (mod.stuck) {
                // Revoking interrupted it: normally the worker is idle again within moments.
                std::unique_lock lock(mod.jobMutex);
                if (mod.doneCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return mod.done; })) {
                    mod.kind = Plugin::Kind::Quit;
                    lock.unlock();
                    mod.jobCv.notify_all();
                    mod.thread.join();
                } else {
                    lock.unlock();
                    mod.thread.detach();
                }
            } else {
                {
                    std::lock_guard lock(mod.jobMutex);
                    mod.kind = Plugin::Kind::Quit;
                }
                mod.jobCv.notify_all();
                mod.thread.join();
            }
        }
        {
            std::lock_guard lock(mutex);
            mod.commands.clear();
            mod.keys.clear();
            mod.layers.clear();
            mod.settings.clear();
            mod.intentFilters.clear();
            mod.runtimeIcon.clear();
        }
        layerVersion++;
        commandVersion++;
    }

    void ModManager::ReadMeta(Plugin& mod) {
        ModInfo meta;
        meta.path = mod.path;
        meta.size = mod.size;
        meta.displayName = mod.name;
        uint32_t permissions = static_cast<uint32_t>(abi::Permission::Storage);
        const fs::path base = fs::path(mod.path).replace_extension("");
        std::error_code ec;
        std::string manifestIcon;
        std::ifstream in(base.string() + ".json", std::ios::binary);
        if (in) {
            const std::string text((std::istreambuf_iterator<char>(in)), {});
            ui::Json json;
            std::string error;
            if (ui::Json::Parse(text, json, error) && json.IsObject()) {
                const auto get = [&](const char* key) {
                    const ui::Json* v = json.Find(key);
                    return v ? v->StringOr("") : std::string();
                };
                if (!get("name").empty())
                    meta.displayName = get("name");
                meta.description = get("description");
                meta.version = get("version");
                meta.author = get("author");
                meta.website = get("website");
                manifestIcon = get("icon");
                if (const ui::Json* list = json.Find("permissions"); list && list->IsArray())
                    for (const ui::Json& item : list->array) {
                        const std::string p = item.StringOr("");
                        if (p == "network")
                            permissions |= static_cast<uint32_t>(abi::Permission::Network);
                        else if (p == "packets")
                            permissions |= static_cast<uint32_t>(abi::Permission::Packets);
                        else if (p == "control")
                            permissions |= static_cast<uint32_t>(abi::Permission::Control);
                        else if (p == "files")
                            permissions |= static_cast<uint32_t>(abi::Permission::Files);
                    }
            }
        }
        if (permissions & static_cast<uint32_t>(abi::Permission::Network))
            meta.permissions.push_back("network");
        if (permissions & static_cast<uint32_t>(abi::Permission::Packets))
            meta.permissions.push_back("packets");
        if (permissions & static_cast<uint32_t>(abi::Permission::Control))
            meta.permissions.push_back("control");
        if (permissions & static_cast<uint32_t>(abi::Permission::Files))
            meta.permissions.push_back("files");
        meta.permissions.push_back("storage");
        // Icon: the manifest's "icon", then name.png next to the mod, then a pack-style icon in its data folder.
        {
            std::vector<fs::path> candidates;
            fs::path rel;
            if (!manifestIcon.empty() && SafeRelative(manifestIcon, rel))
                candidates.push_back(fs::path(directory) / rel);
            candidates.push_back(base.string() + ".png");
            candidates.push_back(fs::path(directory) / (mod.name + ".data") / "pack_icon.png");
            candidates.push_back(fs::path(directory) / (mod.name + ".data") / "icon.png");
            for (const fs::path& c : candidates)
                if (fs::is_regular_file(c, ec)) {
                    meta.iconPath = c.string();
                    break;
                }
        }
        std::lock_guard lock(mutex);
        mod.requested = permissions & DangerousMask();
        mod.safePermissions = permissions & ~DangerousMask();
        mod.meta = std::move(meta);
        ApplyApproval(mod);
    }

    void ModManager::Load(Plugin& mod) {
        mod.error.clear();
        mod.revoked = false;
        mod.stuck = false;
        ReadMeta(mod);
        mod.storage = (fs::path(directory) / (mod.name + ".data")).string();
        LoadKeys(mod);
        LoadSettings(mod);
        if (!ReadFile(mod.path, mod.wasm)) {
            std::lock_guard lock(mutex);
            mod.error = "cannot read file";
            return;
        }
        mod.subs = 0xFFFFFFFFu;
        Spawn(mod);
        {
            std::lock_guard lock(mod.jobMutex);
            mod.done = false;
            mod.kind = Plugin::Kind::Load;
        }
        mod.jobCv.notify_all();
        std::unique_lock lock(mod.jobMutex);
        const bool finished = mod.doneCv.wait_for(lock, std::chrono::milliseconds(4000), [&] { return mod.done; });
        if (!finished) {
            lock.unlock();
            Revoke(mod, "unresponsive: the plugin did not finish loading");
            return;
        }
        if (!mod.jobError.empty()) {
            const std::string message = mod.jobError;
            lock.unlock();
            {
                std::lock_guard guard(mutex);
                mod.error = message;
            }
            std::cout << "[mods] " << mod.name << ": " << message << std::endl;
            Unload(mod);
            return;
        }
        lock.unlock();
        {
            std::lock_guard guard(mutex);
            mod.loads++;
            mod.loaded = true;
        }
        std::cout << "[mods] loaded " << mod.name << (mod.loads > 1 ? " (reloaded)" : "") << std::endl;
        layerVersion++;
    }

    void ModManager::Scan() {
        std::error_code ec;
        std::vector<std::string> present;
        std::vector<std::string> forced;
        bool all;
        std::vector<std::string> off;
        {
            std::lock_guard lock(mutex);
            forced.swap(reloadNames);
            all = reloadAll;
            reloadAll = false;
            off = disabled;
        }
        for (const auto& entry : fs::directory_iterator(directory, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".wasm")
                continue;
            const std::string name = entry.path().stem().string();
            present.push_back(name);
            const auto stamp = entry.last_write_time(ec);
            const uintmax_t size = entry.file_size(ec);
            Plugin* mod = Find(name);
            if (!mod) {
                auto created = std::make_unique<Plugin>();
                created->name = name;
                created->path = entry.path().string();
                mod = created.get();
                std::lock_guard lock(mutex);
                mods.push_back(std::move(created));
            }
            if (mod->meta.path.empty())
                ReadMeta(*mod);
            {
                std::lock_guard lock(mutex);
                ApplyApproval(*mod);
            }
            mod->enabled = std::find(off.begin(), off.end(), name) == off.end() && !mod->needsApproval;
            const bool first = mod->stamp == fs::file_time_type::min();
            const bool force = all || std::find(forced.begin(), forced.end(), name) != forced.end();
            const bool changed = stamp != mod->stamp || size != mod->size;
            const bool alive = mod->loaded || mod->thread.joinable();
            if (!mod->enabled) {
                if (alive || mod->stuck)
                    Unload(*mod);
                if (mod->stuck) {
                    mod->stuck = false;
                    mod->revoked = false;
                }
                mod->stamp = stamp;
                mod->size = size;
                {
                    std::lock_guard lock(mutex);
                    mod->error.clear();
                }
                ReadMeta(*mod);
                continue;
            }
            const bool needLoad = !mod->loaded && mod->error.empty() && !mod->stuck && !mod->thread.joinable();
            const bool reload = force || (changed && (autoReload || first || !mod->loaded));
            if (!reload && !needLoad)
                continue;
            if (reload && !first)
                std::cout << "[mods] " << name << (force ? " reloading" : " changed, reloading") << std::endl;
            if (alive || mod->stuck) {
                const bool wasStuck = mod->stuck;
                Unload(*mod);
                if (wasStuck) {
                    std::lock_guard lock(mutex);
                    for (auto it = mods.begin(); it != mods.end(); ++it)
                        if (it->get() == mod) {
                            auto fresh = std::make_unique<Plugin>();
                            fresh->name = mod->name;
                            fresh->path = mod->path;
                            fresh->loads = mod->loads;
                            fresh->enabled = mod->enabled;
                            graveyard.push_back(std::move(*it));
                            *it = std::move(fresh);
                            mod = it->get();
                            break;
                        }
                }
            }
            mod->stamp = stamp;
            mod->size = size;
            Load(*mod);
        }
        std::vector<std::unique_ptr<Plugin>> gone;
        {
            std::lock_guard lock(mutex);
            for (auto it = mods.begin(); it != mods.end();) {
                if (std::find(present.begin(), present.end(), (*it)->name) == present.end()) {
                    gone.push_back(std::move(*it));
                    it = mods.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& mod : gone) {
            Unload(*mod);
            if (mod->stuck)
                graveyard.push_back(std::move(mod));
        }
    }

    void ModManager::Update() {
        if (!running || tlsPlugin)
            return;
        const auto now = Clock::now();
        if (lastScan != Clock::time_point{} && now - lastScan < std::chrono::milliseconds(500))
            return;
        lastScan = now;
        Scan();
    }

    // ------------------------------------------------------------------------------------------
    // Settings.
    // ------------------------------------------------------------------------------------------
    namespace {
        std::string FormatNumber(double v) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.6g", v);
            return buf;
        }

        bool ParseNumber(const std::string& text, double& out) {
            if (text.empty())
                return false;
            char* end = nullptr;
            out = std::strtod(text.c_str(), &end);
            return end && *end == 0 && std::isfinite(out);
        }

        // Turns text from a mod or the UI into the canonical text for this kind of setting.
        bool NormalizeSetting(const abi::SettingDef& def, const std::string& in, std::string& out) {
            switch (def.kind) {
            case abi::SettingKind::Toggle: {
                std::string v = in;
                std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (v == "true" || v == "1" || v == "on" || v == "yes") {
                    out = "true";
                    return true;
                }
                if (v == "false" || v == "0" || v == "off" || v == "no") {
                    out = "false";
                    return true;
                }
                return false;
            }
            case abi::SettingKind::Integer:
            case abi::SettingKind::Number: {
                double v = 0;
                if (!ParseNumber(in, v))
                    return false;
                const double lo = std::min(def.min, def.max), hi = std::max(def.min, def.max);
                v = std::clamp(v, lo, hi);
                double step = def.step;
                if (def.kind == abi::SettingKind::Integer)
                    step = std::max(1.0, std::round(step));
                if (step > 0)
                    v = std::clamp(lo + std::round((v - lo) / step) * step, lo, hi);
                if (def.kind == abi::SettingKind::Integer)
                    v = std::round(v);
                out = FormatNumber(v);
                return true;
            }
            case abi::SettingKind::Choice:
                if (std::find(def.choices.begin(), def.choices.end(), in) == def.choices.end())
                    return false;
                out = in;
                return true;
            default:
                return false;
            }
        }

        bool ValidSettingId(const std::string& id) {
            if (id.empty() || id.size() > 64)
                return false;
            for (const char c : id)
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-'))
                    return false;
            return true;
        }
    }

    std::string ModManager::SettingsFile(const Plugin& mod) const { return (fs::path(directory) / (mod.name + ".settings.json")).string(); }

    void ModManager::LoadSettings(Plugin& mod) {
        mod.savedSettings.clear();
        std::ifstream in(SettingsFile(mod), std::ios::binary);
        if (!in)
            return;
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        ui::Json json;
        std::string error;
        if (!ui::Json::Parse(text, json, error) || !json.IsObject())
            return;
        for (const auto& entry : json.object)
            if (entry.second.IsString())
                mod.savedSettings[entry.first] = entry.second.string;
    }

    void ModManager::SaveSettings(Plugin& mod) {
        if (!mod.settingsDirty)
            return;
        mod.settingsDirty = false;
        std::string text = "{\n";
        bool first = true;
        for (const auto& entry : mod.savedSettings) {
            if (!first)
                text += ",\n";
            first = false;
            text += "  \"" + Escape(entry.first) + "\": \"" + Escape(entry.second) + "\"";
        }
        text += "\n}\n";
        std::ofstream out(SettingsFile(mod), std::ios::binary | std::ios::trunc);
        out << text;
    }

    bool ModManager::HasSettings(const std::string& plugin) const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        for (const auto& mod : mods)
            if (mod->name == plugin)
                return mod->loaded && !mod->settings.empty();
        return false;
    }

    std::vector<abi::SettingDef> ModManager::Settings(const std::string& plugin) const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        for (const auto& mod : mods)
            if (mod->name == plugin)
                return mod->settings;
        return {};
    }

    bool ModManager::ChangeSetting(const std::string& plugin, const std::string& id, const std::string& value) {
        Plugin* mod = Find(plugin);
        if (!mod)
            return false;
        abi::EventSettingChanged event;
        event.id = id;
        {
            std::lock_guard lock(mutex);
            abi::SettingDef* def = nullptr;
            for (auto& d : mod->settings)
                if (d.id == id)
                    def = &d;
            if (!def)
                return false;
            event.kind = def->kind;
            if (def->kind == abi::SettingKind::Button) {
                event.value = "pressed";
            } else {
                std::string normal;
                if (!NormalizeSetting(*def, value, normal) || normal == def->value)
                    return false;
                def->value = normal;
                mod->savedSettings[id] = normal;
                mod->settingsDirty = true;
                if (def->restart)
                    mod->restartPending = true;
                event.value = normal;
            }
        }
        if (mod->loaded && (mod->subs.load() & (1u << static_cast<uint32_t>(abi::Event::SettingChanged)))) {
            abi::Writer w;
            w.write(event);
            bool cancelled = false;
            Call(*mod, abi::Event::SettingChanged, w, 250.0, cancelled);
        }
        return true;
    }

    void ModManager::ResetSettings(const std::string& plugin) {
        for (const abi::SettingDef& def : Settings(plugin))
            if (def.kind != abi::SettingKind::Button && def.kind != abi::SettingKind::Label && def.kind != abi::SettingKind::Header)
                ChangeSetting(plugin, def.id, def.fallback);
    }

    void ModManager::SettingsClosed(const std::string& plugin) {
        Plugin* mod = Find(plugin);
        if (!mod)
            return;
        SaveSettings(*mod);
        bool reload = false;
        {
            std::lock_guard lock(mutex);
            reload = mod->restartPending;
            mod->restartPending = false;
        }
        if (reload)
            ReloadMod(plugin);
    }

    void ModManager::RegisterSettingOps() {
        Register(abi::Op::SettingDefine, [this](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            abi::SettingDef def = r.read<abi::SettingDef>();
            if (!r.Ok() || !ValidSettingId(def.id) || def.label.size() > 160 || def.description.size() > 600 || def.choices.size() > 64 ||
                def.choiceLabels.size() > 64 || def.icon.size() > 200)
                return abi::Status::BadArguments;
            if (def.kind == abi::SettingKind::Choice && def.choices.empty())
                return abi::Status::BadArguments;
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::lock_guard lock(mutex);
            // Saved value first, then the one the mod passed, then its default.
            std::string chosen;
            const auto saved = mod->savedSettings.find(def.id);
            if (!(saved != mod->savedSettings.end() && NormalizeSetting(def, saved->second, chosen)) &&
                !NormalizeSetting(def, def.value, chosen) && !NormalizeSetting(def, def.fallback, chosen))
                chosen = def.value;
            if (def.fallback.empty())
                def.fallback = chosen;
            def.value = chosen;
            for (auto& d : mod->settings)
                if (d.id == def.id) {
                    d = def;
                    w.write(def.value);
                    return abi::Status::Ok;
                }
            if (mod->settings.size() >= 160)
                return abi::Status::Failed;
            mod->settings.push_back(def);
            w.write(def.value);
            return abi::Status::Ok;
        });

        Register(abi::Op::SettingGet, [this](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            const std::string id = r.str();
            if (!r.Ok())
                return abi::Status::BadArguments;
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::lock_guard lock(mutex);
            for (const auto& d : mod->settings)
                if (d.id == id) {
                    w.raw(d.value.data(), d.value.size());
                    return abi::Status::Ok;
                }
            return abi::Status::NotFound;
        });

        Register(abi::Op::SettingSet, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string id = r.str();
            const std::string value = r.str();
            if (!r.Ok())
                return abi::Status::BadArguments;
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::lock_guard lock(mutex);
            for (auto& d : mod->settings)
                if (d.id == id) {
                    std::string normal;
                    if (!NormalizeSetting(d, value, normal))
                        return abi::Status::BadArguments;
                    d.value = normal;
                    mod->savedSettings[id] = normal;
                    mod->settingsDirty = true;
                    return abi::Status::Ok;
                }
            return abi::Status::NotFound;
        });

        Register(abi::Op::SettingList, [this](CallContext& ctx, abi::Reader&, abi::Writer& w) {
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::lock_guard lock(mutex);
            std::vector<abi::SettingEntry> out;
            for (const auto& d : mod->settings)
                if (d.kind != abi::SettingKind::Button && d.kind != abi::SettingKind::Label && d.kind != abi::SettingKind::Header)
                    out.push_back({d.id, d.value});
            w.write(out);
            return abi::Status::Ok;
        });

        Register(abi::Op::ModList, [this](CallContext& ctx, abi::Reader&, abi::Writer& w) {
            std::vector<abi::ModEntry> out;
            for (const ModInfo& info : List()) {
                abi::ModEntry e;
                e.name = info.name;
                e.displayName = info.displayName;
                e.description = info.description;
                e.version = info.version;
                e.author = info.author;
                e.state = info.state;
                e.enabled = info.enabled;
                e.self = info.name == ctx.plugin;
                e.hasSettings = info.hasSettings;
                out.push_back(std::move(e));
            }
            w.write(out);
            return abi::Status::Ok;
        });

        Register(abi::Op::IconSet, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string rel = r.str();
            fs::path safe;
            if (!r.Ok() || !SafeRelative(rel, safe))
                return abi::Status::BadArguments;
            const fs::path full = fs::path(ctx.storageRoot) / safe;
            std::error_code ec;
            if (full.extension() != ".png" || !fs::is_regular_file(full, ec))
                return abi::Status::NotFound;
            std::lock_guard lock(mutex);
            static_cast<Plugin*>(ctx.user)->runtimeIcon = full.string();
            return abi::Status::Ok;
        });
    }

    // ------------------------------------------------------------------------------------------
    // Intents.
    // ------------------------------------------------------------------------------------------
    void ModManager::RegisterHostIntent(std::string action, std::string description, uint32_t permission, HostIntentFn fn) {
        std::lock_guard lock(mutex);
        hostIntents[std::move(action)] = HostIntent{std::move(description), permission, std::move(fn)};
    }

    uint32_t ModManager::SendIntent(abi::Intent intent, const std::string& sender) {
        PendingIntent p;
        p.id = nextIntent++;
        p.sender = sender;
        p.senderPermissions = sender == "perlin" ? 0xFFFFFFFFu : 0u;
        p.intent = std::move(intent);
        const uint32_t id = p.id;
        std::lock_guard lock(mutex);
        intentQueue.push_back(std::move(p));
        return id;
    }

    void ModManager::DeliverIntent(const PendingIntent& p) {
        abi::EventIntentResult result;
        result.request = p.id;
        const std::string& action = p.intent.action;
        const HostIntent* host = nullptr;
        {
            std::lock_guard lock(mutex);
            if (const auto it = hostIntents.find(action); it != hostIntents.end())
                host = &it->second;
        }
        const bool toHost = p.intent.target == "perlin" || (p.intent.target.empty() && host);
        if (toHost) {
            result.handler = "perlin";
            if (!host) {
                result.outcome = abi::IntentOutcome::Unhandled;
            } else if (host->permission && p.sender != "perlin" && (p.senderPermissions & host->permission) != host->permission) {
                result.outcome = abi::IntentOutcome::Denied;
            } else {
                IntentExtras extras;
                result.outcome = host->fn(p.sender, p.intent, extras);
                result.extras = std::move(extras);
            }
        } else {
            std::vector<Plugin*> targets;
            {
                std::lock_guard lock(mutex);
                for (const auto& mod : mods) {
                    if (!mod->loaded || mod->stuck)
                        continue;
                    if (!p.intent.target.empty()) {
                        if (mod->name == p.intent.target)
                            targets.push_back(mod.get());
                        continue;
                    }
                    for (const auto& f : mod->intentFilters)
                        if (f.action == action) {
                            targets.push_back(mod.get());
                            break;
                        }
                }
            }
            const uint32_t bit = 1u << static_cast<uint32_t>(abi::Event::Intent);
            for (Plugin* mod : targets) {
                if (!(mod->subs.load() & bit))
                    continue;
                abi::EventIntent event;
                event.request = p.id;
                event.sender = p.sender;
                event.intent = p.intent;
                abi::Writer w;
                w.write(event);
                {
                    std::lock_guard lock(mutex);
                    mod->replyingTo = p.id;
                    mod->replied = false;
                    mod->replyExtras.clear();
                }
                bool cancelled = false;
                if (!Call(*mod, abi::Event::Intent, w, 250.0, cancelled))
                    continue;
                std::lock_guard lock(mutex);
                if (result.outcome != abi::IntentOutcome::Handled) {
                    result.outcome = abi::IntentOutcome::Handled;
                    result.handler = mod->name;
                    if (mod->replied)
                        result.extras = mod->replyExtras;
                }
                mod->replyingTo = 0;
            }
        }
        if (p.sender == "perlin")
            return;
        Plugin* sender = Find(p.sender);
        if (sender && sender->loaded && !sender->stuck && (sender->subs.load() & (1u << static_cast<uint32_t>(abi::Event::IntentResult)))) {
            abi::Writer w;
            w.write(result);
            bool cancelled = false;
            Call(*sender, abi::Event::IntentResult, w, 250.0, cancelled);
        }
    }

    void ModManager::Pump() {
        if (tlsPlugin)
            return;
        std::vector<PendingIntent> batch;
        {
            std::lock_guard lock(mutex);
            const size_t take = std::min<size_t>(intentQueue.size(), 32);
            batch.assign(std::make_move_iterator(intentQueue.begin()), std::make_move_iterator(intentQueue.begin() + static_cast<std::ptrdiff_t>(take)));
            intentQueue.erase(intentQueue.begin(), intentQueue.begin() + static_cast<std::ptrdiff_t>(take));
        }
        for (const PendingIntent& p : batch)
            DeliverIntent(p);
        const auto now = Clock::now();
        if (now - lastFlush > std::chrono::milliseconds(500)) {
            lastFlush = now;
            std::lock_guard lock(mutex);
            for (auto& mod : mods)
                SaveSettings(*mod);
        }
    }

    void ModManager::RegisterIntentOps() {
        Register(abi::Op::IntentRegister, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::IntentFilter f = r.read<abi::IntentFilter>();
            if (!r.Ok() || f.action.empty() || f.action.size() > 128 || f.description.size() > 300)
                return abi::Status::BadArguments;
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::lock_guard lock(mutex);
            for (auto& existing : mod->intentFilters)
                if (existing.action == f.action) {
                    existing = f;
                    return abi::Status::Ok;
                }
            if (mod->intentFilters.size() >= 64)
                return abi::Status::Failed;
            mod->intentFilters.push_back(f);
            return abi::Status::Ok;
        });

        Register(abi::Op::IntentSend, [this](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            abi::Intent intent = r.read<abi::Intent>();
            if (!r.Ok() || intent.action.empty() || intent.action.size() > 128 || intent.extras.size() > 32 || intent.target.size() > 64)
                return abi::Status::BadArguments;
            size_t bytes = 0;
            for (const auto& e : intent.extras) {
                bytes += e.key.size();
                if (const auto* s = std::get_if<std::string>(&e.value))
                    bytes += s->size();
                else if (const auto* b = std::get_if<std::vector<uint8_t>>(&e.value))
                    bytes += b->size();
            }
            if (bytes > (256u << 10))
                return abi::Status::BadArguments;
            PendingIntent p;
            p.id = nextIntent++;
            p.sender = ctx.plugin;
            p.senderPermissions = ctx.permissions;
            p.intent = std::move(intent);
            std::lock_guard lock(mutex);
            size_t mine = 0;
            for (const auto& q : intentQueue)
                if (q.sender == ctx.plugin)
                    mine++;
            if (mine >= 64 || intentQueue.size() >= 256)
                return abi::Status::Unavailable;
            w.write(p.id);
            intentQueue.push_back(std::move(p));
            return abi::Status::Ok;
        });

        Register(abi::Op::IntentReply, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const uint32_t request = r.get<uint32_t>();
            std::vector<abi::Extra> extras = r.read<std::vector<abi::Extra>>();
            if (!r.Ok() || extras.size() > 32)
                return abi::Status::BadArguments;
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::lock_guard lock(mutex);
            if (mod->replyingTo == 0 || mod->replyingTo != request)
                return abi::Status::NotFound;
            mod->replyExtras = std::move(extras);
            mod->replied = true;
            return abi::Status::Ok;
        });

        Register(abi::Op::IntentQuery, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const std::string action = r.str();
            if (!r.Ok())
                return abi::Status::BadArguments;
            std::vector<abi::IntentHandler> out;
            std::lock_guard lock(mutex);
            if (const auto it = hostIntents.find(action); it != hostIntents.end())
                out.push_back({"perlin", it->second.description, true});
            for (const auto& mod : mods) {
                if (!mod->loaded)
                    continue;
                for (const auto& f : mod->intentFilters)
                    if (f.action == action)
                        out.push_back({mod->name, f.description, false});
            }
            w.write(out);
            return abi::Status::Ok;
        });
    }

    void ModManager::RegisterCore() {
        Register(abi::Op::Log, [](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const int32_t level = r.get<int32_t>();
            const std::string text = r.str();
            if (!r.Ok())
                return abi::Status::BadArguments;
            static const char* const names[] = {"debug", "info", "warn", "error"};
            std::cout << "[mod:" << ctx.plugin << ":" << names[std::clamp(level, 0, 3)] << "] " << text.substr(0, 2000) << std::endl;
            return abi::Status::Ok;
        });

        Register(abi::Op::Clock, [](CallContext&, abi::Reader&, abi::Writer& w) {
            w.put<int64_t>(UnixMs());
            return abi::Status::Ok;
        });

        Register(abi::Op::Meta, [this](CallContext& ctx, abi::Reader&, abi::Writer& w) {
            abi::Meta meta;
            {
                std::lock_guard lock(mutex);
                meta.gameVersion = gameVersion;
                meta.platform = platform;
            }
            meta.plugin = ctx.plugin;
            meta.permissions = abi::Permissions::from_bits(ctx.permissions);
            meta.nowMs = UnixMs();
            meta.tick = tickCounter.load();
            w.write(meta);
            return abi::Status::Ok;
        });

        const uint32_t storage = static_cast<uint32_t>(abi::Permission::Storage);
        const auto resolve = [](CallContext& ctx, const std::string& rel, fs::path& out, bool allowEmpty = false) {
            fs::path relative;
            if (rel.empty() && allowEmpty)
                relative = "";
            else if (!SafeRelative(rel, relative))
                return false;
            out = fs::path(ctx.storageRoot) / relative;
            return true;
        };

        Register(abi::Op::StorageRead, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            fs::path path;
            if (!resolve(ctx, r.str(), path))
                return abi::Status::BadArguments;
            std::error_code ec;
            if (!fs::is_regular_file(path, ec))
                return abi::Status::NotFound;
            if (fs::file_size(path, ec) > (16u << 20))
                return abi::Status::Failed;
            std::vector<uint8_t> bytes;
            std::ifstream in(path, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), {});
            w.raw(bytes.data(), bytes.size());
            return abi::Status::Ok;
        }, storage);

        Register(abi::Op::StorageWrite, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            fs::path path;
            if (!resolve(ctx, r.str(), path))
                return abi::Status::BadArguments;
            const std::vector<uint8_t> bytes = r.bytes();
            if (!r.Ok() || bytes.size() > (16u << 20))
                return abi::Status::BadArguments;
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
                return abi::Status::Failed;
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return out ? abi::Status::Ok : abi::Status::Failed;
        }, storage);

        Register(abi::Op::StorageExists, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            fs::path path;
            if (!resolve(ctx, r.str(), path))
                return abi::Status::BadArguments;
            std::error_code ec;
            w.boolean(fs::exists(path, ec));
            return abi::Status::Ok;
        }, storage);

        Register(abi::Op::StorageRemove, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            fs::path path;
            if (!resolve(ctx, r.str(), path))
                return abi::Status::BadArguments;
            std::error_code ec;
            if (!fs::exists(path, ec))
                return abi::Status::NotFound;
            fs::remove_all(path, ec);
            return ec ? abi::Status::Failed : abi::Status::Ok;
        }, storage);

        Register(abi::Op::StorageMakeDir, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            fs::path path;
            if (!resolve(ctx, r.str(), path))
                return abi::Status::BadArguments;
            std::error_code ec;
            fs::create_directories(path, ec);
            return ec ? abi::Status::Failed : abi::Status::Ok;
        }, storage);

        Register(abi::Op::StorageList, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            fs::path path;
            if (!resolve(ctx, r.str(), path, true))
                return abi::Status::BadArguments;
            std::error_code ec;
            std::vector<std::string> names;
            if (fs::is_directory(path, ec))
                for (const auto& entry : fs::directory_iterator(path, ec)) {
                    std::string name = entry.path().filename().string();
                    if (entry.is_directory(ec))
                        name += "/";
                    names.push_back(std::move(name));
                    if (names.size() >= 4096)
                        break;
                }
            std::sort(names.begin(), names.end());
            w.put<uint32_t>(static_cast<uint32_t>(names.size()));
            for (const auto& name : names)
                w.str(name);
            return abi::Status::Ok;
        }, storage);

        Register(abi::Op::CommandRegister, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            PluginCommand cmd;
            cmd.plugin = ctx.plugin;
            cmd.name = r.str();
            cmd.usage = r.str();
            cmd.description = r.str();
            if (!r.Ok() || cmd.name.empty() || cmd.name.size() > 64 || cmd.name.find_first_of(" \t/") != std::string::npos)
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            for (auto& c : mod->commands)
                if (c.name == cmd.name) {
                    c = cmd;
                    commandVersion++;
                    return abi::Status::Ok;
                }
            if (mod->commands.size() >= 128)
                return abi::Status::Failed;
            mod->commands.push_back(std::move(cmd));
            commandVersion++;
            return abi::Status::Ok;
        });

        Register(abi::Op::CommandUnregister, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::erase_if(mod->commands, [&](const PluginCommand& c) { return c.name == name; });
            commandVersion++;
            return abi::Status::Ok;
        });

        Register(abi::Op::KeyRegister, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            PluginKey key;
            key.plugin = ctx.plugin;
            key.id = r.str();
            key.label = r.str();
            const std::string fallback = r.str();
            if (!r.Ok() || key.id.empty() || key.id.size() > 64)
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            const auto saved = mod->savedKeys.find(key.id);
            key.binding = saved != mod->savedKeys.end() ? saved->second : fallback;
            for (auto& k : mod->keys)
                if (k.id == key.id) {
                    k.label = key.label;
                    return abi::Status::Ok;
                }
            if (mod->keys.size() >= 64)
                return abi::Status::Failed;
            mod->keys.push_back(std::move(key));
            return abi::Status::Ok;
        });

        Register(abi::Op::UiSubmit, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            const int32_t z = r.get<int32_t>();
            std::vector<uint8_t> commands = r.bytes();
            if (!r.Ok() || name.empty() || name.size() > 64 || commands.size() > (512u << 10))
                return abi::Status::BadArguments;
            {
                std::lock_guard lock(mutex);
                auto* mod = static_cast<Plugin*>(ctx.user);
                if (mod->layers.size() >= 16 && !mod->layers.count(name))
                    return abi::Status::Failed;
                UiLayer& layer = mod->layers[name];
                layer.plugin = ctx.plugin;
                layer.name = name;
                layer.z = z;
                layer.commands = std::move(commands);
                layer.serial = ++mod->serial;
            }
            layerVersion++;
            return abi::Status::Ok;
        });

        Register(abi::Op::UiClear, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            {
                std::lock_guard lock(mutex);
                static_cast<Plugin*>(ctx.user)->layers.erase(name);
            }
            layerVersion++;
            return abi::Status::Ok;
        });

        RegisterSettingOps();
        RegisterIntentOps();
    }
}
