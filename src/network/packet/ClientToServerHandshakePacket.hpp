#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct ClientToServerHandshakePacket {};

    PERLIN_PACKET_ID(ClientToServerHandshakePacket, 4);

}
