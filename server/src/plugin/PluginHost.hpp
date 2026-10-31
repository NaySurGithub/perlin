#pragma once
#include "plugin/PluginApi.hpp"

#include "perlin/host.hpp"

#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace perlin::server {
    // runs server plugins: the same wasm modules, SDK and abi (mods/sdk/perlin/abi_table.h) as client mods,
    // answering the calls marked Server or Both and raising the server's events
    class PluginHost {
    public:
        struct Info {
            std::string name;
            std::string file;
            std::string version;
            std::string author;
            std::string description;
            std::string engine;
            bool enabled = false;
            std::string error;
        };

        explicit PluginHost(PluginApi& api);
        ~PluginHost();
        PluginHost(const PluginHost&) = delete;
        PluginHost& operator=(const PluginHost&) = delete;

        // loads every *.wasm in `dir`; a plugin that fails to load is reported and skipped
        void LoadDirectory(const std::string& dir);
        bool LoadFile(const std::string& path, std::string& error);
        void UnloadAll();

        // true when a plugin cancelled; the first plugin to cancel stops the rest
        template <abi::Event E> bool Dispatch(const abi::payload_t<E>& payload) {
            const abi::Writer w = abi::host::Encode<E>(payload);
            return DispatchEncoded(E, {w.Data(), w.Size()});
        }
        // lets hot paths skip building a payload nobody reads
        bool AnyoneListens(abi::Event event) const;
        // to the plugin that registered the command; false if it has none or trapped
        bool DispatchCommand(const std::string& plugin, uint64_t player, const std::string& command,
                             const std::string& args);
        std::vector<Info> List() const;

    private:
        struct Plugin;
        struct CallContext {
            Plugin& plugin;
            PluginApi& api;
        };

        void RegisterOps();
        bool DispatchEncoded(abi::Event event, std::span<const uint8_t> payload);
        // false (and the plugin disabled) when it trapped
        bool CallPlugin(Plugin& plugin, abi::Event event, std::span<const uint8_t> payload, uint32_t& result);

        std::vector<std::unique_ptr<Plugin>> plugins;
        abi::host::OpTable<CallContext> ops;
        PluginApi& api;
    };
}
