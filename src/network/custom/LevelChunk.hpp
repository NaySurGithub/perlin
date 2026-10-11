#pragma once
#include "network/Stream.hpp"

namespace perlin::proto {

    struct LevelChunkPacket {
        enum class Mode { Inline, Request };

        static constexpr uint32_t kId = 58;
        static constexpr const char* kName = "level_chunk";

        int32_t x = 0, z = 0, dimension = 0;
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
            const int n = requestLimit + 1;
            return n < dimensionSlots ? n : dimensionSlots;
        }
    };

    void Write(net::WriteStream&, const LevelChunkPacket&);
    bool Read(net::ReadStream&, LevelChunkPacket&);

}
