#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct StartGamePacket {
        int64_t entityId = 0;
        uint64_t runtimeId = 0;
        int32_t gameMode = 0;
        Vec3 position;
        float pitch = 0;
        float yaw = 0;
        Rest tail;
    };

    PERLIN_PACKET_ID(StartGamePacket, 11);

}
