#pragma once
#include "network/Stream.hpp"

namespace perlin::proto {

    enum class SubChunkResult : uint8_t {
        Undefined = 0,
        Success = 1,
        ChunkNotFound = 2,
        InvalidDimension = 3,
        PlayerNotFound = 4,
        YIndexOutOfBounds = 5,
        SuccessAllAir = 6,
    };

    struct SubChunkEntry {
        int8_t dx = 0, dy = 0, dz = 0;
        uint8_t result = 0;
        std::vector<uint8_t> payload;
        uint8_t heightMapType = 0;
        uint8_t renderHeightMapType = 0;
    };

    struct SubChunkPacket {
        static constexpr uint32_t kId = 174;
        static constexpr const char* kName = "subchunk";

        bool cacheEnabled = false;
        int32_t dimension = 0;
        int32_t x = 0, y = 0, z = 0;
        std::vector<SubChunkEntry> entries;
        int layout = -1;
        bool exact = false;
    };

    void Write(net::WriteStream&, const SubChunkPacket&);
    bool Read(net::ReadStream&, SubChunkPacket&);

}
