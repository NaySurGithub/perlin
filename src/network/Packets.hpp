#pragma once
#include "network/PacketStream.hpp"

#include "network/packet/RequestNetworkSettingsPacket.hpp"
#include "network/packet/NetworkSettingsPacket.hpp"
#include "network/packet/PlayStatusPacket.hpp"
#include "network/packet/DisconnectPacket.hpp"
#include "network/packet/ServerToClientHandshakePacket.hpp"
#include "network/packet/ClientToServerHandshakePacket.hpp"
#include "network/packet/ResourcePacksInfoPacket.hpp"
#include "network/packet/ResourcePackStackPacket.hpp"
#include "network/packet/ResourcePacksReadyForValidationPacket.hpp"
#include "network/packet/ResourcePackClientResponsePacket.hpp"
#include "network/packet/ClientCacheStatusPacket.hpp"
#include "network/packet/RequestChunkRadiusPacket.hpp"
#include "network/packet/SetLocalPlayerAsInitializedPacket.hpp"
#include "network/packet/StartGamePacket.hpp"
#include "network/packet/SetTimePacket.hpp"
#include "network/packet/SetSpawnPositionPacket.hpp"
#include "network/packet/UpdateBlockPacket.hpp"
#include "network/packet/MovePlayerPacket.hpp"
#include "network/packet/RespawnPacket.hpp"
#include "network/packet/UpdateAbilitiesPacket.hpp"
#include "network/packet/CorrectPlayerMovePredictionPacket.hpp"
#include "network/packet/PlayerActionPacket.hpp"
#include "network/packet/PacketViolationWarningPacket.hpp"
#include "network/packet/NetworkStackLatencyPacket.hpp"
#include "network/packet/TextPacket.hpp"
#include "network/packet/CommandRequestPacket.hpp"
#include "network/packet/CommandOutputPacket.hpp"

#include "network/serializer/RequestNetworkSettingsPacketSerializer.hpp"
#include "network/serializer/NetworkSettingsPacketSerializer.hpp"
#include "network/serializer/PlayStatusPacketSerializer.hpp"
#include "network/serializer/DisconnectPacketSerializer.hpp"
#include "network/serializer/ServerToClientHandshakePacketSerializer.hpp"
#include "network/serializer/ClientToServerHandshakePacketSerializer.hpp"
#include "network/serializer/ResourcePacksInfoPacketSerializer.hpp"
#include "network/serializer/ResourcePackStackPacketSerializer.hpp"
#include "network/serializer/ResourcePacksReadyForValidationPacketSerializer.hpp"
#include "network/serializer/ResourcePackClientResponsePacketSerializer.hpp"
#include "network/serializer/ClientCacheStatusPacketSerializer.hpp"
#include "network/serializer/RequestChunkRadiusPacketSerializer.hpp"
#include "network/serializer/SetLocalPlayerAsInitializedPacketSerializer.hpp"
#include "network/serializer/StartGamePacketSerializer.hpp"
#include "network/serializer/SetTimePacketSerializer.hpp"
#include "network/serializer/SetSpawnPositionPacketSerializer.hpp"
#include "network/serializer/UpdateBlockPacketSerializer.hpp"
#include "network/serializer/MovePlayerPacketSerializer.hpp"
#include "network/serializer/RespawnPacketSerializer.hpp"
#include "network/serializer/UpdateAbilitiesPacketSerializer.hpp"
#include "network/serializer/CorrectPlayerMovePredictionPacketSerializer.hpp"
#include "network/serializer/PlayerActionPacketSerializer.hpp"
#include "network/serializer/PacketViolationWarningPacketSerializer.hpp"
#include "network/serializer/NetworkStackLatencyPacketSerializer.hpp"
#include "network/serializer/TextPacketSerializer.hpp"
#include "network/serializer/CommandRequestPacketSerializer.hpp"
#include "network/serializer/CommandOutputPacketSerializer.hpp"

#include "network/packet/PlayerAuthInputPacket.hpp"
#include "network/packet/LevelChunkPacket.hpp"
#include "network/packet/SubChunkPacket.hpp"
#include "network/packet/SubChunkRequestPacket.hpp"
#include "network/serializer/PlayerAuthInputPacketSerializer.hpp"
#include "network/serializer/LevelChunkPacketSerializer.hpp"
#include "network/serializer/SubChunkPacketSerializer.hpp"
#include "network/serializer/SubChunkRequestPacketSerializer.hpp"

#include <random>

namespace perlin::net {

    inline Uuid RandomUuidV4() {
        static std::random_device rd;
        static std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) | rd());
        uint8_t raw[16];
        for (int i = 0; i < 16; i++)
            raw[i] = (uint8_t)rng();
        raw[6] = (uint8_t)((raw[6] & 0x0F) | 0x40);
        raw[8] = (uint8_t)((raw[8] & 0x3F) | 0x80);
        Uuid u;
        for (int i = 0; i < 8; i++)
            u.bytes[i] = raw[15 - i];
        for (int i = 0; i < 8; i++)
            u.bytes[8 + i] = raw[7 - i];
        return u;
    }

}
