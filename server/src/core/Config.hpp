#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin::server {

    struct FlatLayer {
        std::string block;
        int count = 1;
    };

    struct ServerConfig {
        std::string name = "Perlin Server";
        std::string motd = "A Perlin server";
        std::string levelName = "world";
        std::string dataDir = ".";
        // require an Xbox Live identity: logins must chain to Mojang's root key and, over nethernet, the offer must be signed by that identity.
        // off = anyone can join under any name (fine for a lan or singleplayer, not for a public server)
        bool xboxAuth = false;
        // extra or replacement trusted root keys (base64 der); empty = Mojang's
        std::vector<std::string> authRootKeys;
        std::string pluginDir;
        // where block_states.json and block_destroy.tsv are; empty = look beside the executable and in the source tree
        std::string blockDataDir;
        uint32_t maxPlayers = 20;
        uint32_t viewDistance = 12;
        uint32_t tickRate = 20;
        // 0 survival, 1 creative, 2 adventure
        int gameMode = 0;
        uint64_t seed = 0;
        // legacy list of operator names or xuids; ops.json is the real list now and is merged with this one
        std::vector<std::string> operators;
        // only players listed in whitelist.json (by name or xuid) may join; off by default
        bool whitelist = false;
        // answer lan discovery broadcasts (udp 7551) so the world shows up in the Friends tab of vanilla clients on the same network
        bool lanDiscovery = true;

        // nethernet signalling: http on this port, webrtc data channels behind it
        bool nethernet = true;
        uint16_t port = 19132;
        std::string bindAddress = "0.0.0.0";
        std::vector<std::string> iceServers;
        // one udp port for every player (needs one forwarded port); off = a port per connection from `port` upward
        bool iceUdpMux = true;
        // address webrtc binds its udp sockets to; empty = all interfaces. set it on hosts without ipv6 or with several nics
        std::string iceBindAddress;

        // "flat" only for now; later vanilla once the terrain is replicated
        // "flat", or "vanilla" for the Java terrain port (needs java_data)
        std::string generator = "flat";
        // the Java Edition 26.3 client jar (or a folder with data/minecraft) the vanilla generator reads its worldgen data from
        std::string javaData;
        // bottom to top from the dimension floor
        std::vector<FlatLayer> flatLayers = {{"minecraft:bedrock", 1}, {"minecraft:dirt", 2}, {"minecraft:grass_block", 1}};
        // auto = gpu when a device starts and passes its self check, else cpu
        std::string terrainBackend = "auto";

        // run without a network listener (embedded singleplayer)
        bool loopbackOnly = false;

        static ServerConfig Load(const std::string& path, std::string* error = nullptr);
        bool Save(const std::string& path) const;
    };
}
