#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace perlin::mods {
    struct HostState {
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
        int gameMode = 0;
        bool connected = false;
        bool onGround = false, sprinting = false, sneaking = false;
        double vx = 0, vy = 0, vz = 0;
    };

    struct Outbound {
        enum class Kind { Chat, Hud } kind = Kind::Chat;
        std::string text;
    };

    struct ModInfo {
        std::string name;
        bool enabled = true;
        std::string state;
        std::string error;
        int loads = 0;
        std::string displayName, description, version, author, website, iconPath, path;
        std::vector<std::string> hooks;
        uint64_t size = 0;
    };

    class ModManager {
    public:
        static ModManager& Instance();

        void Start(const std::string& directory);
        void Stop();
        void SetDisabled(const std::vector<std::string>& names);
        void SetHostState(const HostState& state);
        void SendChat(const std::string& message);
        std::vector<Outbound> TakeOutbound();
        std::vector<ModInfo> List() const;
        std::string Directory() const { return directory; }
        void ReloadAll();
        void ReloadMod(const std::string& name);
        void SetAutoReload(bool value) { autoReload = value; }

        mutable std::mutex mutex;
        HostState host;
        std::vector<Outbound> outbound;

    private:
        ModManager() = default;
        ~ModManager();
        struct Mod;
        void Run();
        void Scan();
        void Load(Mod& mod);
        void Unload(Mod& mod);
        void Publish(Mod& mod);
        void ReadMeta(Mod& mod);
        void Tick(Mod& mod, float dt);

        std::string directory;
        std::thread worker;
        std::atomic<bool> running{false};
        std::condition_variable wake;
        std::vector<std::unique_ptr<Mod>> mods;
        std::vector<std::string> disabled;
        std::vector<std::string> pendingChat;
        bool reloadAll = false;
        std::vector<std::string> reloadNames;
        bool lastConnected = false;
        std::atomic<bool> autoReload{true};
        friend struct Host;
    };
}
