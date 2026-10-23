#pragma once
#include "network/XboxAuth.hpp"

#include <string>
#include <vector>

namespace perlin::featured {

    struct Server {
        std::string name;
        std::string description;
        std::string host;
        uint16_t port = 19132;
        std::string logoUrl;
        bool ranked = false;  // official featured entry (rather than a creator server)
        int rank = 0;
    };

    // The same list the Minecraft "Servers" screen shows: Mojang's gatherings discovery catalog, read with the
    // signed-in account's Minecraft services token. Entries that are only reachable through an experience id (no
    // host and port) are skipped, since Perlin joins by address.
    bool List(xbox::Account& account, std::vector<Server>& out, std::string& error);

}
