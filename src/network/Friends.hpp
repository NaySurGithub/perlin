#pragma once
#include "network/XboxAuth.hpp"

#include <string>
#include <vector>

namespace perlin::friends {

    struct Friend {
        std::string xuid;
        std::string gamertag;
        bool online = false;
        bool playingMinecraft = false;
        std::string pictureUrl; // the gamerpic
        bool hosting = false;   // has a world open that can be joined
        std::string worldName;
        int players = 0, maxPlayers = 0;
        std::string presence; // "Minecraft", "Online", "Last seen ..." or similar, as Xbox reports it
    };

    // The signed-in account's Xbox friends with their presence (PeopleHub), Minecraft players first.
    bool List(xbox::Account& account, std::vector<Friend>& out, std::string& error);

}
