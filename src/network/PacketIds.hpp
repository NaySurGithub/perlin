#pragma once
#include <cstdint>

namespace perlin {

    namespace PacketId {
        constexpr uint32_t PlayStatus = 2;
        constexpr uint32_t Disconnect = 5;
        constexpr uint32_t ResourcePacksInfo = 6;
        constexpr uint32_t ResourcePackStack = 7;
        constexpr uint32_t ResourcePackClientResponse = 8;
        constexpr uint32_t Text = 9;
        constexpr uint32_t SetTime = 10;
        constexpr uint32_t StartGame = 11;
        constexpr uint32_t MovePlayer = 19;
        constexpr uint32_t UpdateBlock = 21;
        constexpr uint32_t PlayerAction = 36;
        constexpr uint32_t SetSpawnPosition = 43;
        constexpr uint32_t LevelChunk = 58;
        constexpr uint32_t RequestChunkRadius = 69;
        constexpr uint32_t ChunkRadiusUpdated = 70;
        constexpr uint32_t SetLocalPlayerAsInitialized = 113;
        constexpr uint32_t NetworkStackLatency = 115;
        constexpr uint32_t NetworkChunkPublisherUpdate = 121;
        constexpr uint32_t ClientCacheStatus = 129;
        constexpr uint32_t ClientCacheBlobStatus = 135;
        constexpr uint32_t NetworkSettings = 143;
        constexpr uint32_t PlayerAuthInput = 144;
        constexpr uint32_t PacketViolationWarning = 156;
        constexpr uint32_t SubChunk = 174;
        constexpr uint32_t SubChunkRequest = 175;
    }

}
