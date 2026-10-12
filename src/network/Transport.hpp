#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin {

    // What Perlin speaks: the generated packet set is for this Bedrock protocol / game version.
    constexpr int kBedrockProtocol = 2193;
    constexpr const char* kBedrockVersion = "1.26.52";

    struct NetherNetServerInfo {
        std::string name;
        int protocol = 0;
        std::string version;
        std::string level;
        int players = 0;
        int maxPlayers = 0;
        int gameType = 0;
    };

    // A connection that carries Bedrock packet batches (the bytes after the 0xFE game-packet header).
    class Transport {
    public:
        virtual ~Transport() = default;

        virtual void Disconnect() = 0;
        virtual bool SendReliable(const std::vector<uint8_t>& data) = 0;
        // May be reliable on transports that cannot do better (RakNet keeps everything ordered because of encryption).
        virtual bool SendUnreliable(const std::vector<uint8_t>& data) = 0;
        virtual bool TryReceive(std::vector<uint8_t>& out) = 0;
        virtual bool IsConnected() const = 0;
        virtual std::string LastError() const = 0;
        virtual const NetherNetServerInfo& Info() const = 0;
    };

}
