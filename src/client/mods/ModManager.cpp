#include "client/mods/ModManager.hpp"

#include <wasm3.h>

#include "client/ui/Json.hpp"

#include <algorithm>
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

        bool InRange(const void* mem, uint32_t ptr, uint32_t len) {
            if (len == 0)
                return true;
            return static_cast<uint64_t>(ptr) + len <= m3_GetMemorySizeAt(mem);
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
        std::vector<PluginCommand> commands;
        std::vector<PluginKey> keys;
        std::map<std::string, UiLayer> layers;
        std::map<std::string, std::string> savedKeys;
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

        IM3Environment env = nullptr;
        IM3Runtime runtime = nullptr;
        IM3Module module = nullptr;
        IM3Function fnEvent = nullptr, fnAlloc = nullptr, fnFree = nullptr, fnSubs = nullptr;
    };

    struct HostBridge {
        static ModManager* manager;

        static const void* Call(IM3Runtime runtime, IM3ImportContext ctx, uint64_t* sp, void* mem) {
            int32_t* ret = reinterpret_cast<int32_t*>(sp++);
            const uint32_t op = static_cast<uint32_t>(*sp++);
            const uint32_t inPtr = static_cast<uint32_t>(*sp++);
            const uint32_t inLen = static_cast<uint32_t>(*sp++);
            const uint32_t outPtr = static_cast<uint32_t>(*sp++);
            const uint32_t outCap = static_cast<uint32_t>(*sp++);
            auto* plugin = static_cast<ModManager::Plugin*>(ctx->userdata);
            if (plugin->revoked)
                return kRevoked;
            if (!InRange(mem, inPtr, inLen) || !InRange(mem, outPtr, outCap)) {
                *ret = static_cast<int32_t>(abi::Status::BadArguments);
                return m3Err_none;
            }
            const int32_t rc = manager->HostCall(*plugin, op, static_cast<uint8_t*>(mem) + inPtr, inLen, static_cast<uint8_t*>(mem) + outPtr, outCap);
            if (rc == INT32_MIN)
                return kRevoked;
            *ret = rc;
            return m3Err_none;
        }
    };
    ModManager* HostBridge::manager = nullptr;

    namespace {
        template <int Code>
        const void* WasiConst(IM3Runtime, IM3ImportContext, uint64_t* sp, void*) {
            *reinterpret_cast<int32_t*>(sp) = Code;
            return m3Err_none;
        }

        const void* WasiExit(IM3Runtime, IM3ImportContext, uint64_t*, void*) { return kExit; }

        const void* WasiSizes(IM3Runtime, IM3ImportContext, uint64_t* sp, void* mem) {
            int32_t* ret = reinterpret_cast<int32_t*>(sp++);
            const uint32_t a = static_cast<uint32_t>(*sp++);
            const uint32_t b = static_cast<uint32_t>(*sp++);
            if (!InRange(mem, a, 4) || !InRange(mem, b, 4)) {
                *ret = 21;
                return m3Err_none;
            }
            const uint32_t zero = 0;
            std::memcpy(static_cast<uint8_t*>(mem) + a, &zero, 4);
            std::memcpy(static_cast<uint8_t*>(mem) + b, &zero, 4);
            *ret = 0;
            return m3Err_none;
        }

        const void* WasiClock(IM3Runtime, IM3ImportContext, uint64_t* sp, void* mem) {
            int32_t* ret = reinterpret_cast<int32_t*>(sp++);
            const uint32_t id = static_cast<uint32_t>(*sp++);
            sp++;
            const uint32_t out = static_cast<uint32_t>(*sp++);
            if (!InRange(mem, out, 8)) {
                *ret = 21;
                return m3Err_none;
            }
            uint64_t ns;
            if (id == 0)
                ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            else
                ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
            std::memcpy(static_cast<uint8_t*>(mem) + out, &ns, 8);
            *ret = 0;
            return m3Err_none;
        }

        const void* WasiClockRes(IM3Runtime, IM3ImportContext, uint64_t* sp, void* mem) {
            int32_t* ret = reinterpret_cast<int32_t*>(sp++);
            sp++;
            const uint32_t out = static_cast<uint32_t>(*sp++);
            if (!InRange(mem, out, 8)) {
                *ret = 21;
                return m3Err_none;
            }
            const uint64_t res = 1000;
            std::memcpy(static_cast<uint8_t*>(mem) + out, &res, 8);
            *ret = 0;
            return m3Err_none;
        }

        const void* WasiRandom(IM3Runtime, IM3ImportContext, uint64_t* sp, void* mem) {
            int32_t* ret = reinterpret_cast<int32_t*>(sp++);
            const uint32_t ptr = static_cast<uint32_t>(*sp++);
            const uint32_t len = static_cast<uint32_t>(*sp++);
            if (!InRange(mem, ptr, len)) {
                *ret = 21;
                return m3Err_none;
            }
            thread_local std::mt19937_64 rng{std::random_device{}()};
            auto* bytes = static_cast<uint8_t*>(mem) + ptr;
            for (uint32_t i = 0; i < len; i++)
                bytes[i] = static_cast<uint8_t>(rng());
            *ret = 0;
            return m3Err_none;
        }

        const void* WasiFdWrite(IM3Runtime, IM3ImportContext ctx, uint64_t* sp, void* mem) {
            int32_t* ret = reinterpret_cast<int32_t*>(sp++);
            const uint32_t fd = static_cast<uint32_t>(*sp++);
            const uint32_t iovs = static_cast<uint32_t>(*sp++);
            const uint32_t count = static_cast<uint32_t>(*sp++);
            const uint32_t written = static_cast<uint32_t>(*sp++);
            if (fd != 1 && fd != 2) {
                *ret = 8;
                return m3Err_none;
            }
            if (count > 1024 || !InRange(mem, iovs, count * 8) || !InRange(mem, written, 4)) {
                *ret = 21;
                return m3Err_none;
            }
            std::string text;
            uint32_t total = 0;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t ptr, len;
                std::memcpy(&ptr, static_cast<uint8_t*>(mem) + iovs + i * 8, 4);
                std::memcpy(&len, static_cast<uint8_t*>(mem) + iovs + i * 8 + 4, 4);
                if (!InRange(mem, ptr, len)) {
                    *ret = 21;
                    return m3Err_none;
                }
                if (text.size() < 65536)
                    text.append(reinterpret_cast<const char*>(static_cast<uint8_t*>(mem) + ptr), len);
                total += len;
            }
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                text.pop_back();
            if (!text.empty()) {
                auto* plugin = static_cast<ModManager::Plugin*>(ctx->userdata);
                std::cout << "[mod:" << plugin->name << "] " << text << std::endl;
            }
            std::memcpy(static_cast<uint8_t*>(mem) + written, &total, 4);
            *ret = 0;
            return m3Err_none;
        }

        const void* WasiFdstat(IM3Runtime, IM3ImportContext, uint64_t* sp, void* mem) {
            int32_t* ret = reinterpret_cast<int32_t*>(sp++);
            const uint32_t fd = static_cast<uint32_t>(*sp++);
            const uint32_t out = static_cast<uint32_t>(*sp++);
            if (fd > 2 || !InRange(mem, out, 24)) {
                *ret = 8;
                return m3Err_none;
            }
            uint8_t stat[24] = {};
            stat[0] = 2;
            const uint64_t rights = fd == 0 ? (1ull << 1) : (1ull << 6);
            std::memcpy(stat + 8, &rights, 8);
            std::memcpy(static_cast<uint8_t*>(mem) + out, stat, 24);
            *ret = 0;
            return m3Err_none;
        }

        void LinkWasi(IM3Module module, void* user) {
            const auto link = [&](const char* name, const char* sig, M3RawCall fn) {
                m3_LinkRawFunctionEx(module, "wasi_snapshot_preview1", name, sig, fn, user);
            };
            link("args_sizes_get", "i(ii)", WasiSizes);
            link("environ_sizes_get", "i(ii)", WasiSizes);
            link("args_get", "i(ii)", WasiConst<0>);
            link("environ_get", "i(ii)", WasiConst<0>);
            link("clock_time_get", "i(iIi)", WasiClock);
            link("clock_res_get", "i(ii)", WasiClockRes);
            link("random_get", "i(ii)", WasiRandom);
            link("fd_write", "i(iiii)", WasiFdWrite);
            link("fd_fdstat_get", "i(ii)", WasiFdstat);
            link("fd_close", "i(i)", WasiConst<0>);
            link("fd_sync", "i(i)", WasiConst<0>);
            link("fd_datasync", "i(i)", WasiConst<0>);
            link("fd_seek", "i(iIii)", WasiConst<70>);
            link("fd_tell", "i(ii)", WasiConst<70>);
            link("fd_read", "i(iiii)", WasiConst<8>);
            link("fd_pread", "i(iiiIi)", WasiConst<8>);
            link("fd_pwrite", "i(iiiIi)", WasiConst<8>);
            link("fd_readdir", "i(iiiIi)", WasiConst<8>);
            link("fd_renumber", "i(ii)", WasiConst<8>);
            link("fd_advise", "i(iIIi)", WasiConst<8>);
            link("fd_allocate", "i(iII)", WasiConst<8>);
            link("fd_fdstat_set_flags", "i(ii)", WasiConst<8>);
            link("fd_fdstat_set_rights", "i(iII)", WasiConst<8>);
            link("fd_filestat_get", "i(ii)", WasiConst<8>);
            link("fd_filestat_set_size", "i(iI)", WasiConst<8>);
            link("fd_filestat_set_times", "i(iIIi)", WasiConst<8>);
            link("fd_prestat_get", "i(ii)", WasiConst<8>);
            link("fd_prestat_dir_name", "i(iii)", WasiConst<8>);
            link("path_open", "i(iiiiiIIii)", WasiConst<76>);
            link("path_filestat_get", "i(iiiii)", WasiConst<76>);
            link("path_filestat_set_times", "i(iiiiIIi)", WasiConst<76>);
            link("path_create_directory", "i(iii)", WasiConst<76>);
            link("path_remove_directory", "i(iii)", WasiConst<76>);
            link("path_unlink_file", "i(iii)", WasiConst<76>);
            link("path_rename", "i(iiiiii)", WasiConst<76>);
            link("path_readlink", "i(iiiiii)", WasiConst<76>);
            link("path_symlink", "i(iiiii)", WasiConst<76>);
            link("path_link", "i(iiiiiii)", WasiConst<76>);
            link("poll_oneoff", "i(iiii)", WasiConst<52>);
            link("sched_yield", "i()", WasiConst<0>);
            link("proc_exit", "v(i)", WasiExit);
            link("proc_raise", "i(i)", WasiConst<52>);
            link("sock_accept", "i(iii)", WasiConst<52>);
            link("sock_recv", "i(iiiiii)", WasiConst<52>);
            link("sock_send", "i(iiiii)", WasiConst<52>);
            link("sock_shutdown", "i(ii)", WasiConst<52>);
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
            info.state = !mod->enabled ? "disabled" : mod->stuck ? "unresponsive" : !mod->error.empty() ? "error" : mod->loaded ? "running" : "loading";
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
        }
        mod.stuck = true;
        mod.loaded = false;
        {
            std::lock_guard lock(mutex);
            mod.error = why;
            mod.commands.clear();
            mod.keys.clear();
            mod.layers.clear();
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
        if (mod.runtime)
            m3_FreeRuntime(mod.runtime);
        if (mod.env)
            m3_FreeEnvironment(mod.env);
        mod.runtime = nullptr;
        mod.env = nullptr;
        mod.module = nullptr;
        mod.fnEvent = mod.fnAlloc = mod.fnFree = mod.fnSubs = nullptr;
    }

    namespace {
        bool CallEventFn(ModManager&, IM3Function fnEvent, IM3Function fnAlloc, IM3Function fnFree, IM3Module module, uint32_t id,
                         const std::vector<uint8_t>& payload, uint32_t& flags, std::string& error) {
            uint32_t ptr = 0;
            const uint32_t len = static_cast<uint32_t>(payload.size());
            if (len && !fnAlloc) {
                error = "plugin has no perlin_alloc export";
                return false;
            }
            if (len) {
                if (M3Result r = m3_CallV(fnAlloc, len)) {
                    error = std::string("alloc: ") + r;
                    return false;
                }
                if (M3Result r = m3_GetResultsV(fnAlloc, &ptr)) {
                    error = std::string("alloc: ") + r;
                    return false;
                }
                size_t memSize = 0;
                uint8_t* memory = m3_GetMemory(module, &memSize, 0);
                if (!memory || ptr == 0 || static_cast<uint64_t>(ptr) + len > memSize) {
                    error = "alloc returned a bad pointer";
                    return false;
                }
                std::memcpy(memory + ptr, payload.data(), len);
            }
            if (M3Result r = m3_CallV(fnEvent, id, ptr, len)) {
                error = std::string(EventName(id) ? EventName(id) : "event") + ": " + r;
                return false;
            }
            flags = 0;
            m3_GetResultsV(fnEvent, &flags);
            if (len && fnFree)
                m3_CallV(fnFree, ptr);
            return true;
        }
    }

    void ModManager::WorkerLoad(Plugin& mod) {
        mod.jobError.clear();
        mod.env = m3_NewEnvironment();
        mod.runtime = m3_NewRuntime(mod.env, 512 * 1024, &mod);
        IM3Module module = nullptr;
        M3Result result = m3_ParseModule(mod.env, &module, mod.wasm.data(), static_cast<uint32_t>(mod.wasm.size()));
        if (result) {
            mod.jobError = std::string("parse: ") + result;
            return;
        }
        result = m3_LoadModule(mod.runtime, module);
        if (result) {
            mod.jobError = std::string("load: ") + result;
            m3_FreeModule(module);
            return;
        }
        mod.module = module;
        m3_LinkRawFunctionEx(module, "perlin", "call", "i(iiiii)", HostBridge::Call, &mod);
        LinkWasi(module, &mod);
        if (m3_FindFunction(&mod.fnEvent, mod.runtime, "perlin_event") || !mod.fnEvent) {
            mod.jobError = "not a Perlin plugin (missing perlin_event export)";
            return;
        }
        if (m3_FindFunction(&mod.fnAlloc, mod.runtime, "perlin_alloc"))
            mod.fnAlloc = nullptr;
        if (m3_FindFunction(&mod.fnFree, mod.runtime, "perlin_free"))
            mod.fnFree = nullptr;
        if (m3_FindFunction(&mod.fnSubs, mod.runtime, "perlin_subscriptions"))
            mod.fnSubs = nullptr;
        IM3Function init = nullptr;
        if (!m3_FindFunction(&init, mod.runtime, "_initialize") && init) {
            if (M3Result r = m3_CallV(init)) {
                mod.jobError = std::string("_initialize: ") + r;
                return;
            }
        }
        uint32_t flags = 0;
        std::string error;
        if (!CallEventFn(*this, mod.fnEvent, mod.fnAlloc, mod.fnFree, module, static_cast<uint32_t>(abi::Event::Load), {}, flags, error)) {
            mod.jobError = error;
            return;
        }
        if (mod.fnSubs) {
            uint32_t subs = 0;
            if (!m3_CallV(mod.fnSubs))
                m3_GetResultsV(mod.fnSubs, &subs);
            mod.subs = subs;
        }
    }

    void ModManager::WorkerEvent(Plugin& mod) {
        mod.jobError.clear();
        uint32_t flags = 0;
        std::string error;
        if (!CallEventFn(*this, mod.fnEvent, mod.fnAlloc, mod.fnFree, mod.module, static_cast<uint32_t>(mod.event), mod.payload, flags, error)) {
            mod.jobError = error;
            return;
        }
        mod.flags = flags;
        if (mod.fnSubs && mod.event != abi::Event::Frame && mod.event != abi::Event::Unload) {
            uint32_t subs = 0;
            if (!m3_CallV(mod.fnSubs))
                m3_GetResultsV(mod.fnSubs, &subs);
            mod.subs = subs;
        }
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
        mod.loaded = false;
        if (mod.thread.joinable()) {
            if (mod.stuck) {
                mod.thread.detach();
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
                if (const ui::Json* list = json.Find("permissions"); list && list->IsArray())
                    for (const ui::Json& item : list->array) {
                        const std::string p = item.StringOr("");
                        if (p == "network")
                            permissions |= static_cast<uint32_t>(abi::Permission::Network);
                        else if (p == "packets")
                            permissions |= static_cast<uint32_t>(abi::Permission::Packets);
                        else if (p == "control")
                            permissions |= static_cast<uint32_t>(abi::Permission::Control);
                    }
            }
        }
        if (permissions & static_cast<uint32_t>(abi::Permission::Network))
            meta.permissions.push_back("network");
        if (permissions & static_cast<uint32_t>(abi::Permission::Packets))
            meta.permissions.push_back("packets");
        if (permissions & static_cast<uint32_t>(abi::Permission::Control))
            meta.permissions.push_back("control");
        meta.permissions.push_back("storage");
        if (fs::exists(base.string() + ".png", ec))
            meta.iconPath = base.string() + ".png";
        std::lock_guard lock(mutex);
        mod.permissions = permissions;
        mod.meta = std::move(meta);
    }

    void ModManager::Load(Plugin& mod) {
        mod.error.clear();
        mod.revoked = false;
        mod.stuck = false;
        ReadMeta(mod);
        mod.storage = (fs::path(directory) / (mod.name + ".data")).string();
        LoadKeys(mod);
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
            mod->enabled = std::find(off.begin(), off.end(), name) == off.end();
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
            meta.permissions = ctx.permissions;
            meta.nowMs = UnixMs();
            meta.tick = tickCounter.load();
            meta.Write(w);
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
    }
}
