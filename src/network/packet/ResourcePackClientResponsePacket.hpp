#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct ResourcePackClientResponsePacket {
        uint8_t status = 4;
        std::vector<std::string> packs;
    };

    PERLIN_PACKET_ID(ResourcePackClientResponsePacket, 8);

}
