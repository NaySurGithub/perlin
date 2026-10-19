#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "perlin/abi.hpp"

namespace perlin::mods {
    struct ModInfo {
        std::string name;
        bool enabled = true;
        std::string state;
        std::string error;
        int loads = 0;
        std::string displayName, description, version, author, website, iconPath, path;
        std::vector<std::string> hooks;
        std::vector<std::string> permissions;
        uint64_t size = 0;
        double lastEventMs = 0;
        std::string arch; // "wasm64" or "wasm32" once loaded
        bool hasSettings = false;
        uint32_t requested = 0;  // dangerous permission bits the manifest asks for
        bool needsApproval = false; // asks for permissions the user has not allowed yet
    };

    // A mod's reply to an intent the game or another mod sent it, plus who handled it.
    using IntentExtras = std::vector<abi::Extra>;
    // Runs on the main thread. `sender` is the sending mod ("perlin" for the game itself).
    using HostIntentFn = std::function<abi::IntentOutcome(const std::string& sender, const abi::Intent&, IntentExtras& result)>;

    struct PendingIntent {
        uint32_t id = 0;
        std::string sender;
        uint32_t senderPermissions = 0;
        abi::Intent intent;
    };

    struct PluginCommand {
        std::string plugin, name, usage, description;
    };

    struct PluginKey {
        std::string plugin, id, label, binding;
    };

    struct UiLayer {
        std::string plugin, name;
        int z = 0;
        std::vector<uint8_t> commands;
        uint64_t serial = 0;
    };

    struct CallContext {
        std::string plugin;
        uint32_t permissions = 0;
        std::string storageRoot;
        void* user = nullptr;
    };

    using OpHandler = std::function<abi::Status(CallContext&, abi::Reader&, abi::Writer&)>;

    class ModManager {
    public:
        static ModManager& Instance();

        void Start(const std::string& directory);
        void Stop();
        void SetDisabled(const std::vector<std::string>& names);
        // "name=bits" entries: the dangerous permissions the user allowed for each mod. A mod asking for more than that
        // stays off until it is approved in the Mods screen.
        void SetApproved(const std::vector<std::string>& entries);
        static uint32_t DangerousMask();
        std::vector<ModInfo> List() const;
        std::string Directory() const { return directory; }
        void ReloadAll();
        void ReloadMod(const std::string& name);
        void SetAutoReload(bool value) { autoReload = value; }

        void SetEnvironment(const std::string& gameVersion, const std::string& platform);
        void Register(abi::Op op, OpHandler handler, uint32_t permission = 0);
        void Update();

        bool Dispatch(abi::Event event, const abi::Writer& payload, double budgetMs = 250.0);
        bool Dispatch(abi::Event event, double budgetMs = 250.0) { return Dispatch(event, abi::Writer{}, budgetMs); }
        bool WantsEvent(abi::Event event) const;
        void DispatchTo(const std::string& plugin, abi::Event event, const abi::Writer& payload, double budgetMs = 250.0);

        std::vector<PluginCommand> Commands() const;
        std::vector<PluginKey> Keys() const;
        void SetKeyBinding(const std::string& plugin, const std::string& id, const std::string& binding);
        std::vector<UiLayer> Layers() const;
        uint64_t LayerVersion() const { return layerVersion.load(); }
        uint64_t CommandVersion() const { return commandVersion.load(); }
        bool HasPlugins() const;

        // Settings a mod declares (shown on its settings page). Values are persisted per mod.
        bool HasSettings(const std::string& plugin) const;
        std::vector<abi::SettingDef> Settings(const std::string& plugin) const;
        // Applies a value chosen in the UI: validated, stored, saved, and sent to the mod as SettingChanged.
        // Returns whether the stored value changed.
        bool ChangeSetting(const std::string& plugin, const std::string& id, const std::string& value);
        void ResetSettings(const std::string& plugin);
        // The settings page was closed: saves, and reloads the mod if a changed setting needs that.
        void SettingsClosed(const std::string& plugin);

        // Intents. The game registers handlers for its own actions; mods register filters for theirs.
        void RegisterHostIntent(std::string action, std::string description, uint32_t permission, HostIntentFn fn);
        // Delivers queued intents and flushes settings. Call once per frame from the main thread.
        void Pump();
        // Sends an intent as the game ("perlin").
        uint32_t SendIntent(abi::Intent intent, const std::string& sender = "perlin");
        bool HasPermission(const std::string& plugin, abi::Permission p) const;
        uint64_t Tick() const { return tickCounter.load(); }
        void SetTick(uint64_t t) { tickCounter = t; }
        void NoteLayerChange() { layerVersion++; }

        struct Plugin;
        std::mutex mutex;

    private:
        ModManager() = default;
        ~ModManager();
        void Scan();
        void Load(Plugin& mod);
        void Unload(Plugin& mod);
        void ReadMeta(Plugin& mod);
        void ApplyApproval(Plugin& mod);
        std::map<std::string, uint32_t> approved;
        bool Call(Plugin& mod, abi::Event event, const abi::Writer& payload, double budgetMs, bool& cancelled);
        int32_t HostCall(Plugin& mod, uint32_t op, const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t outCap);
        void Revoke(Plugin& mod, const char* why);
        void Retire(Plugin& mod);
        std::string KeysFile(const Plugin& mod) const;
        void RegisterCore();
        void LoadKeys(Plugin& mod);
        void SaveKeys(const Plugin& mod) const;
        Plugin* Find(const std::string& name) const;
        static void Worker(ModManager* self, Plugin* mod);
        void WorkerLoad(Plugin& mod);
        void WorkerEvent(Plugin& mod);
        void WorkerFree(Plugin& mod);
        void Spawn(Plugin& mod);
        std::string SettingsFile(const Plugin& mod) const;
        void LoadSettings(Plugin& mod);
        void SaveSettings(Plugin& mod);
        void DeliverIntent(const PendingIntent& pending);
        void RegisterSettingOps();
        void RegisterIntentOps();

        std::string directory;
        std::atomic<bool> running{false};
        std::vector<std::unique_ptr<Plugin>> mods;
        std::vector<std::unique_ptr<Plugin>> graveyard;
        std::vector<std::string> disabled;
        std::unordered_map<uint32_t, std::pair<OpHandler, uint32_t>> ops;
        std::string gameVersion, platform;
        bool reloadAll = false;
        std::vector<std::string> reloadNames;
        std::atomic<bool> autoReload{true};
        std::atomic<uint64_t> layerVersion{1};
        std::atomic<uint64_t> commandVersion{1};
        std::atomic<uint64_t> tickCounter{0};
        std::chrono::steady_clock::time_point lastScan{};
        struct HostIntent {
            std::string description;
            uint32_t permission = 0;
            HostIntentFn fn;
        };
        std::map<std::string, HostIntent> hostIntents;
        std::vector<PendingIntent> intentQueue;
        std::atomic<uint32_t> nextIntent{1};
        std::chrono::steady_clock::time_point lastFlush{};
        friend struct HostBridge;
    };
}
