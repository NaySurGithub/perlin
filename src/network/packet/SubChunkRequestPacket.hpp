#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct SubChunkRequestPacket {
        int32_t dimension = 0;
        int32_t baseX = 0, baseY = 0, baseZ = 0;
        std::vector<std::array<int8_t, 3>> offsets;

        static SubChunkRequestPacket ForColumn(int dimension, int cx, int cz, int firstSubY, int count) {
            SubChunkRequestPacket p;
            p.dimension = dimension;
            p.baseX = cx;
            p.baseZ = cz;
            for (int i = 0; i < count; i++)
                p.offsets.push_back({(int8_t)0, (int8_t)(firstSubY + i), (int8_t)0});
            return p;
        }
        static SubChunkRequestPacket ForSlot(int dimension, int cx, int subY, int cz) {
            return ForColumn(dimension, cx, cz, subY, 1);
        }
    };

    PERLIN_PACKET_ID(SubChunkRequestPacket, 175);

}
