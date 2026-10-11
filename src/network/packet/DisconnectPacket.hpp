#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct DisconnectPacket {
        int32_t reason = 0;
        bool hideMessage = false;
        std::string message;
        std::string filteredMessage;
    };

    PERLIN_PACKET_ID(DisconnectPacket, 5);

}
