#pragma once
#include "network/FriendWorld.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perlin::lan {

    // A game found on the local network by Minecraft's own LAN discovery (encrypted UDP on port 7551).
    struct Host {
        uint64_t networkId = 0; // NetherNet network id: where signals to the host are addressed
        std::string ip;
        uint16_t port = 0;
        std::string name;
        std::string version;
        std::string level;
        int protocol = 0;
        int players = 0, maxPlayers = 0;
        int gameType = 0;
        bool onlineAuth = false;
        bool selfSignedAuth = false;
    };

    // One UDP socket on the discovery port's network. It finds games (broadcast request, responses) and doubles as the
    // NetherNet signalling channel to the games it found, so a LAN game is dialled through the same code as a friend's.
    class Session : public friendworld::Signaling {
    public:
        static std::shared_ptr<Session> Open(std::string& error);
        ~Session() override;

        // Asks the network who is hosting (call repeatedly while waiting).
        void Scan();
        std::vector<Host> Hosts() const;
        // Waits until the game with this network id has answered.
        bool WaitFor(uint64_t networkId, int timeoutMs);

        bool Send(const std::string& toNetworkId, const std::string& text, bool waitDelivery, std::string& error) override;
        void SetHandler(std::function<void(const std::string&, const std::string&)> handler) override;
        bool Credentials(std::vector<friendworld::IceServerInfo>& servers, std::string& error) override;
        bool Alive() const override;
        void Close() override;

        struct Impl;

    private:
        Session();
        std::unique_ptr<Impl> impl;
    };

}
