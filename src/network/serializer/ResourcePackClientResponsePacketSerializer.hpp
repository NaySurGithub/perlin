#pragma once
#include "network/packet/ResourcePackClientResponsePacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<ResourcePackClientResponsePacket> {
        static void Write(WriteStream& s, const ResourcePackClientResponsePacket& p) {
            s.u8(p.status);
            s.le16(static_cast<uint16_t>(p.packs.size()));
            for (const std::string& pack : p.packs)
                s.str(pack);
        }
        static bool Read(ReadStream& s, ResourcePackClientResponsePacket& p) {
            s.u8(p.status);
            uint16_t count = 0;
            s.le16(count);
            p.packs.clear();
            for (uint16_t i = 0; i < count && s.ok; i++) {
                std::string pack;
                s.str(pack);
                p.packs.push_back(std::move(pack));
            }
            return s.ok;
        }
    };

}
