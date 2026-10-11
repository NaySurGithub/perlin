#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct ResourcePacksReadyForValidationPacket {};

    PERLIN_PACKET_ID(ResourcePacksReadyForValidationPacket, 340);

}
