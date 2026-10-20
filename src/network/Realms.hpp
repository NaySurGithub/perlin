#pragma once
#include "network/XboxAuth.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace perlin::realms {

    struct Realm {
        int64_t id = 0;
        std::string name;
        std::string owner;  // owner's gamertag, when the service says
        std::string motd;
        std::string state;  // "OPEN" or "CLOSED"
        bool expired = false;
        bool member = false;
        int maxPlayers = 0;
    };

    struct Address {
        std::string host;
        uint16_t port = 19132;
        std::string protocol; // "DEFAULT" (RakNet) or a NetherNet kind
    };

    // The Realms the signed-in account owns or was invited to (GET /worlds on the Bedrock Realms front end, the way
    // gophertunnel's realms package does it).
    bool List(xbox::Account& account, std::vector<Realm>& out, std::string& error);

    // POST /worlds/<id>/join: where the Realm is running now. Waits while the service says it is starting
    // (Retry-After), for up to about 40 seconds.
    bool Join(xbox::Account& account, int64_t id, Address& out, std::string& error);

}
