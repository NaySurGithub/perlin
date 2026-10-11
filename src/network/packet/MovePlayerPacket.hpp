#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct MovePlayerPacket {
        uint64_t runtimeId = 0;
        Vec3 position;
        float pitch = 0;
        float yaw = 0;
        float headYaw = 0;
        uint8_t mode = 0;
        bool onGround = false;
        uint64_t ridingRuntimeId = 0;
        int32_t teleportCause = 0;
        int32_t teleportSourceType = 0;
        uint64_t tick = 0;
    };

    PERLIN_PACKET_ID(MovePlayerPacket, 19);

}
