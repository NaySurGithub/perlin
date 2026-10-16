#include "client/mods/ModManager.hpp"

#include <wasm3.h>

#include "client/ui/Json.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace perlin::mods {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    struct ModManager::Mod {
        std::string name;
        std::string path;
        fs::file_time_type stamp{};
        uintmax_t size = 0;
        bool enabled = true;
        bool loaded = false;
        bool stuck = false;
        std::string error;
        int loads = 0;
        IM3Environment env = nullptr;
        IM3Runtime runtime = nullptr;
        IM3Function onTick = nullptr, onUnload = nullptr, onChat = nullptr, alloc = nullptr, onConnect = nullptr, onDisconnect = nullptr;
        std::vector<uint8_t> bytes;
        bool onLoadFound = false;
        std::atomic<int64_t> busySince{0};
        std::string pubError;
        ModInfo pubMeta;
        ModInfo meta;
        int pubLoads = 0;
        bool pubLoaded = false;
    };

    struct Host {
        static ModManager* manager;
        static Clock::time_point start;
    };
    ModManager* Host::manager = nullptr;
    Clock::time_point Host::start = Clock::now();

    namespace {
        int64_t NowMs();

        std::string Str(void* mem, uint32_t ptr, uint32_t len) {
            const size_t size = m3_GetMemorySizeAt(mem);
            if (static_cast<uint64_t>(ptr) + len > size)
                return {};
            return std::string(static_cast<const char*>(mem) + ptr, len);
        }

        m3ApiRawFunction(HostLog) {
            m3ApiGetArg(uint32_t, ptr);
            m3ApiGetArg(uint32_t, len);
            const std::string text = Str(_mem, ptr, len);
            std::cout << "[mod:" << static_cast<const char*>(_ctx->userdata) << "] " << text << std::endl;
            m3ApiSuccess();
        }

        m3ApiRawFunction(HostChat) {
            m3ApiGetArg(uint32_t, ptr);
            m3ApiGetArg(uint32_t, len);
            const std::string text = Str(_mem, ptr, len);
            if (!text.empty() && Host::manager) {
                std::lock_guard lock(Host::manager->mutex);
                Host::manager->outbound.push_back({Outbound::Kind::Chat, text});
            }
            m3ApiSuccess();
        }

        m3ApiRawFunction(HostHud) {
            m3ApiGetArg(uint32_t, ptr);
            m3ApiGetArg(uint32_t, len);
            const std::string text = Str(_mem, ptr, len);
            if (Host::manager) {
                std::lock_guard lock(Host::manager->mutex);
                Host::manager->outbound.push_back({Outbound::Kind::Hud, text});
            }
            m3ApiSuccess();
        }

        template <int Axis>
        m3ApiRawFunction(HostPos) {
            m3ApiReturnType(double);
            double v = 0;
            if (Host::manager) {
                std::lock_guard lock(Host::manager->mutex);
                v = Axis == 0 ? Host::manager->host.x : Axis == 1 ? Host::manager->host.y : Host::manager->host.z;
            }
            m3ApiReturn(v);
        }

        template <int Axis>
        m3ApiRawFunction(HostLook) {
            m3ApiReturnType(float);
            float v = 0;
            if (Host::manager) {
                std::lock_guard lock(Host::manager->mutex);
                v = Axis == 0 ? Host::manager->host.yaw : Host::manager->host.pitch;
            }
            m3ApiReturn(v);
        }

        m3ApiRawFunction(HostGameMode) {
            m3ApiReturnType(int32_t);
            int v = 0;
            if (Host::manager) {
                std::lock_guard lock(Host::manager->mutex);
                v = Host::manager->host.gameMode;
            }
            m3ApiReturn(v);
        }

        m3ApiRawFunction(HostConnected) {
            m3ApiReturnType(int32_t);
            int v = 0;
            if (Host::manager) {
                std::lock_guard lock(Host::manager->mutex);
                v = Host::manager->host.connected ? 1 : 0;
            }
            m3ApiReturn(v);
        }

        template <int Axis>
        m3ApiRawFunction(HostVel) {
            m3ApiReturnType(double);
            double v = 0;
            if (Host::manager) {
                std::lock_guard lock(Host::manager->mutex);
                v = Axis == 0 ? Host::manager->host.vx : Axis == 1 ? Host::manager->host.vy : Host::manager->host.vz;
            }
            m3ApiReturn(v);
        }

        template <int Flag>
        m3ApiRawFunction(HostFlag) {
            m3ApiReturnType(int32_t);
            int v = 0;
            if (Host::manager) {
                std::lock_guard lock(Host::manager->mutex);
                v = Flag == 0 ? Host::manager->host.onGround : Flag == 1 ? Host::manager->host.sprinting : Host::manager->host.sneaking;
            }
            m3ApiReturn(v);
        }

        m3ApiRawFunction(HostTime) {
            m3ApiReturnType(int64_t);
            m3ApiReturn(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - Host::start).count());
        }

    }

    namespace {
        int64_t NowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        }

        bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            out.assign(std::istreambuf_iterator<char>(in), {});
            return !out.empty();
        }
    }

    ModManager& ModManager::Instance() {
        static ModManager manager;
        return manager;
    }

    ModManager::~ModManager() { Stop(); }

    void ModManager::Start(const std::string& dir) {
        if (running.exchange(true))
            return;
        directory = dir;
        std::error_code ec;
        fs::create_directories(directory, ec);
        Host::manager = this;
        worker = std::thread([this] { Run(); });
    }

    void ModManager::Stop() {
        if (!running.exchange(false))
            return;
        wake.notify_all();
        if (worker.joinable())
            worker.join();
        for (auto& mod : mods)
            Unload(*mod);
        mods.clear();
    }

    void ModManager::SetDisabled(const std::vector<std::string>& names) {
        std::lock_guard lock(mutex);
        disabled = names;
        wake.notify_all();
    }

    void ModManager::SetHostState(const HostState& state) {
        std::lock_guard lock(mutex);
        host = state;
    }

    void ModManager::SendChat(const std::string& message) {
        std::lock_guard lock(mutex);
        pendingChat.push_back(message);
    }

    std::vector<Outbound> ModManager::TakeOutbound() {
        std::lock_guard lock(mutex);
        std::vector<Outbound> out;
        out.swap(outbound);
        return out;
    }

    void ModManager::ReloadAll() {
        std::lock_guard lock(mutex);
        reloadAll = true;
        wake.notify_all();
    }

    void ModManager::ReloadMod(const std::string& name) {
        std::lock_guard lock(mutex);
        reloadNames.push_back(name);
        wake.notify_all();
    }

    std::vector<ModInfo> ModManager::List() const {
        std::lock_guard lock(mutex);
        std::vector<ModInfo> out;
        for (const auto& mod : mods) {
            ModInfo info;
            info = mod->pubMeta;
            info.name = mod->name;
            info.enabled = mod->enabled;
            info.loads = mod->pubLoads;
            info.error = mod->pubError;
            const int64_t busy = mod->busySince.load();
            const bool stuck = busy != 0 && NowMs() - busy > 3000;
            info.state = !mod->enabled ? "disabled" : stuck ? "unresponsive" : !mod->pubError.empty() ? "error" : mod->pubLoaded ? "running" : "loading";
            out.push_back(std::move(info));
        }
        return out;
    }

    void ModManager::ReadMeta(Mod& mod) {
        ModInfo meta;
        meta.path = mod.path;
        meta.size = mod.size;
        meta.displayName = mod.name;
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
            }
        }
        if (fs::exists(base.string() + ".png", ec))
            meta.iconPath = base.string() + ".png";
        if (mod.onLoadFound)
            meta.hooks.push_back("on_load");
        if (mod.onTick)
            meta.hooks.push_back("on_tick");
        if (mod.onChat)
            meta.hooks.push_back("on_chat");
        if (mod.onConnect)
            meta.hooks.push_back("on_connect");
        if (mod.onDisconnect)
            meta.hooks.push_back("on_disconnect");
        if (mod.onUnload)
            meta.hooks.push_back("on_unload");
        mod.meta = std::move(meta);
    }

    void ModManager::Publish(Mod& mod) {
        std::lock_guard lock(mutex);
        mod.pubError = mod.error;
        mod.pubLoads = mod.loads;
        mod.pubLoaded = mod.loaded;
        mod.pubMeta = mod.meta;
    }

    void ModManager::Unload(Mod& mod) {
        if (mod.runtime) {
            if (mod.onUnload && !mod.stuck)
                m3_CallV(mod.onUnload);
            m3_FreeRuntime(mod.runtime);
        }
        if (mod.env)
            m3_FreeEnvironment(mod.env);
        mod.runtime = nullptr;
        mod.env = nullptr;
        mod.onTick = mod.onUnload = mod.onChat = mod.alloc = mod.onConnect = mod.onDisconnect = nullptr;
        mod.onLoadFound = false;
        mod.loaded = false;
    }

    void ModManager::Load(Mod& mod) {
        Unload(mod);
        mod.error.clear();
        mod.stuck = false;
        if (!ReadFile(mod.path, mod.bytes)) {
            mod.error = "cannot read file";
            return;
        }
        mod.env = m3_NewEnvironment();
        mod.runtime = m3_NewRuntime(mod.env, 256 * 1024, &mod);
        IM3Module module = nullptr;
        M3Result result = m3_ParseModule(mod.env, &module, mod.bytes.data(), static_cast<uint32_t>(mod.bytes.size()));
        if (result) {
            mod.error = std::string("parse: ") + result;
            return;
        }
        result = m3_LoadModule(mod.runtime, module);
        if (result) {
            mod.error = std::string("load: ") + result;
            m3_FreeModule(module);
            return;
        }
        const char* const name = mod.name.c_str();
        const auto link = [&](const char* fn, const char* sig, M3RawCall call) { m3_LinkRawFunctionEx(module, "perlin", fn, sig, call, name); };
        link("log", "v(ii)", HostLog);
        link("chat", "v(ii)", HostChat);
        link("hud_text", "v(ii)", HostHud);
        link("player_x", "F()", HostPos<0>);
        link("player_y", "F()", HostPos<1>);
        link("player_z", "F()", HostPos<2>);
        link("player_yaw", "f()", HostLook<0>);
        link("player_pitch", "f()", HostLook<1>);
        link("game_mode", "i()", HostGameMode);
        link("connected", "i()", HostConnected);
        link("time_ms", "I()", HostTime);
        link("vel_x", "F()", HostVel<0>);
        link("vel_y", "F()", HostVel<1>);
        link("vel_z", "F()", HostVel<2>);
        link("on_ground", "i()", HostFlag<0>);
        link("sprinting", "i()", HostFlag<1>);
        link("sneaking", "i()", HostFlag<2>);
        IM3Function onLoad = nullptr;
        if (!m3_FindFunction(&onLoad, mod.runtime, "on_load") && onLoad) {
            mod.onLoadFound = true;
            mod.busySince = NowMs();
            result = m3_CallV(onLoad);
            mod.busySince = 0;
            if (result) {
                mod.error = std::string("on_load: ") + result;
                return;
            }
        }
        if (m3_FindFunction(&mod.onTick, mod.runtime, "on_tick"))
            mod.onTick = nullptr;
        if (m3_FindFunction(&mod.onUnload, mod.runtime, "on_unload"))
            mod.onUnload = nullptr;
        if (m3_FindFunction(&mod.onChat, mod.runtime, "on_chat"))
            mod.onChat = nullptr;
        if (m3_FindFunction(&mod.alloc, mod.runtime, "alloc"))
            mod.alloc = nullptr;
        if (m3_FindFunction(&mod.onConnect, mod.runtime, "on_connect"))
            mod.onConnect = nullptr;
        if (m3_FindFunction(&mod.onDisconnect, mod.runtime, "on_disconnect"))
            mod.onDisconnect = nullptr;
        if (lastConnected && mod.onConnect)
            m3_CallV(mod.onConnect);
        mod.loaded = true;
        mod.loads++;
        std::cout << "[mods] loaded " << mod.name << (mod.loads > 1 ? " (reloaded)" : "") << std::endl;
    }

    void ModManager::Tick(Mod& mod, float dt) {
        if (!mod.loaded || !mod.onTick || !mod.error.empty())
            return;
        mod.busySince = NowMs();
        const M3Result result = m3_CallV(mod.onTick, dt);
        mod.busySince = 0;
        if (result) {
            mod.error = std::string("on_tick: ") + result;
            mod.loaded = false;
            std::cout << "[mods] " << mod.name << " stopped: " << mod.error << std::endl;
            Publish(mod);
        }
    }

    void ModManager::Scan() {
        std::error_code ec;
        std::vector<std::string> present;
        for (const auto& entry : fs::directory_iterator(directory, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".wasm")
                continue;
            const std::string name = entry.path().stem().string();
            present.push_back(name);
            const auto stamp = entry.last_write_time(ec);
            const uintmax_t size = entry.file_size(ec);
            Mod* mod = nullptr;
            {
                std::lock_guard lock(mutex);
                for (auto& m : mods)
                    if (m->name == name)
                        mod = m.get();
                if (!mod) {
                    mods.push_back(std::make_unique<Mod>());
                    mod = mods.back().get();
                    mod->name = name;
                    mod->path = entry.path().string();
                    mod->stamp = fs::file_time_type::min();
                }
                mod->enabled = std::find(disabled.begin(), disabled.end(), name) == disabled.end();
            }
            const bool changed = (stamp != mod->stamp || size != mod->size) && (autoReload || mod->stamp == fs::file_time_type::min() || !mod->loaded);
            if (!mod->enabled) {
                if (mod->loaded || mod->runtime)
                    Unload(*mod);
                mod->stamp = stamp;
                mod->size = size;
                ReadMeta(*mod);
                Publish(*mod);
                continue;
            }
            if (changed || (!mod->loaded && mod->error.empty() && !mod->runtime)) {
                if (changed && mod->stamp != fs::file_time_type::min())
                    std::cout << "[mods] " << name << " changed, reloading" << std::endl;
                mod->stamp = stamp;
                mod->size = size;
                Load(*mod);
                ReadMeta(*mod);
                Publish(*mod);
                if (!mod->error.empty())
                    std::cout << "[mods] " << name << ": " << mod->error << std::endl;
            }
        }
        std::lock_guard lock(mutex);
        for (auto it = mods.begin(); it != mods.end();) {
            if (std::find(present.begin(), present.end(), (*it)->name) == present.end()) {
                Unload(**it);
                it = mods.erase(it);
            } else {
                ++it;
            }
        }
    }

    void ModManager::Run() {
        auto lastScan = Clock::now() - std::chrono::seconds(10);
        auto lastTick = Clock::now();
        while (running) {
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(50));
            }
            if (!running)
                break;
            const auto now = Clock::now();
            bool force = false;
            std::vector<std::string> chat;
            {
                std::lock_guard lock(mutex);
                force = reloadAll;
                reloadAll = false;
                chat.swap(pendingChat);
            }
            std::vector<std::string> names;
            {
                std::lock_guard lock(mutex);
                names.swap(reloadNames);
            }
            if (force)
                for (auto& mod : mods)
                    mod->stamp = fs::file_time_type::min();
            for (const std::string& name : names)
                for (auto& mod : mods)
                    if (mod->name == name)
                        mod->stamp = fs::file_time_type::min();
            force = force || !names.empty();
            if (force || now - lastScan > std::chrono::milliseconds(500)) {
                lastScan = now;
                Scan();
            }
            bool connected;
            {
                std::lock_guard lock(mutex);
                connected = host.connected;
            }
            if (connected != lastConnected) {
                lastConnected = connected;
                for (auto& mod : mods)
                    if (mod->loaded && mod->error.empty()) {
                        IM3Function fn = connected ? mod->onConnect : mod->onDisconnect;
                        if (fn)
                            m3_CallV(fn);
                    }
            }
            for (const std::string& line : chat)
                for (auto& mod : mods)
                    if (mod->loaded && mod->onChat && mod->alloc && mod->error.empty()) {
                        uint32_t ptr = 0;
                        if (m3_CallV(mod->alloc, static_cast<uint32_t>(line.size())) || m3_GetResultsV(mod->alloc, &ptr))
                            continue;
                        size_t size = 0;
                        IM3Module module = m3_GetFunctionModule(mod->alloc);
                        uint8_t* memory = m3_GetMemory(module, &size, 0);
                        if (!memory || static_cast<uint64_t>(ptr) + line.size() > size)
                            continue;
                        std::memcpy(memory + ptr, line.data(), line.size());
                        m3_CallV(mod->onChat, ptr, static_cast<uint32_t>(line.size()));
                    }
            if (now - lastTick >= std::chrono::milliseconds(50)) {
                const float dt = std::chrono::duration<float>(now - lastTick).count();
                lastTick = now;
                for (auto& mod : mods)
                    Tick(*mod, dt);
            }
        }
    }
}
