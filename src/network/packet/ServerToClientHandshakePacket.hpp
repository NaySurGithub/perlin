#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct ServerToClientHandshakePacket {
        std::string token;
    };

    PERLIN_PACKET_ID(ServerToClientHandshakePacket, 3);

}
