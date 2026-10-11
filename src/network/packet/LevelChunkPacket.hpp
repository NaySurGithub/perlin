#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct LevelChunkPacket {
        enum class Mode { Inline, Request };

        int32_t cx = 0, cz = 0, dimension = 0;
        uint32_t subChunkCount = 0;
        bool hasRequestLimit = false;
        int32_t requestLimit = -1;
        bool cacheEnabled = false;
        std::vector<uint64_t> blobHashes;
        std::vector<uint8_t> payload;
        bool isClientBiomeUpdate = false;

        Mode GetMode() const { return hasRequestLimit ? Mode::Request : Mode::Inline; }
        int RequestSlots(int dimensionSlots) const {
            if (!hasRequestLimit)
                return 0;
            if (requestLimit < 0)
                return dimensionSlots;
            int n = requestLimit + 1;
            return n < dimensionSlots ? n : dimensionSlots;
        }
    };

    PERLIN_PACKET_ID(LevelChunkPacket, 58);

}
