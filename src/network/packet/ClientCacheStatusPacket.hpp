#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct ClientCacheStatusPacket {
        bool enabled = false;
    };

    PERLIN_PACKET_ID(ClientCacheStatusPacket, 129);

}
