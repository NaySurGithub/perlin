#pragma once
#include "network/Stream.hpp"

namespace perlin::proto {

    struct SubChunkRequestPacket {
        static constexpr uint32_t kId = 175;
        static constexpr const char* kName = "subchunk_request";

        int32_t dimension = 0;
        std::vector<std::array<int8_t, 3>> offsets;
        int32_t x = 0, y = 0, z = 0;

        static SubChunkRequestPacket ForColumn(int dimension, int cx, int cz, int firstSubY, int count) {
            SubChunkRequestPacket p;
            p.dimension = dimension;
            p.x = cx;
            p.z = cz;
            for (int i = 0; i < count; i++)
                p.offsets.push_back({0, static_cast<int8_t>(firstSubY + i), 0});
            return p;
        }
        static SubChunkRequestPacket ForSlot(int dimension, int cx, int subY, int cz) {
            return ForColumn(dimension, cx, cz, subY, 1);
        }
    };

    void Write(net::WriteStream&, const SubChunkRequestPacket&);
    bool Read(net::ReadStream&, SubChunkRequestPacket&);

}
