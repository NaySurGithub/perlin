#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    namespace CommandOrigin {
        constexpr uint32_t Player = 0;
        constexpr uint32_t DevConsole = 3;
        constexpr uint32_t Test = 4;
    }

    struct CommandRequestPacket {
        std::string command;
        uint32_t originType = 0;
        Uuid originUuid;
        std::string requestId;
        int64_t playerId = 0;
        bool isInternal = false;
        int32_t version = 52;
    };

    PERLIN_PACKET_ID(CommandRequestPacket, 77);

}
