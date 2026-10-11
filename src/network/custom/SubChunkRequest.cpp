#include "network/custom/SubChunkRequest.hpp"

namespace perlin::proto {

    namespace {

        template <class S> void Serialize(S& s, SubChunkRequestPacket& v) {
            s.zigzag(v.dimension);
            uint32_t count = static_cast<uint32_t>(v.offsets.size());
            s.varint(count);
            if constexpr (S::kReading) {
                if (!s.count(count))
                    return;
                v.offsets.assign(count, {});
            }
            for (auto& offset : v.offsets) {
                s.i8(offset[0]);
                s.i8(offset[1]);
                s.i8(offset[2]);
            }
            s.le(v.x);
            s.le(v.y);
            s.le(v.z);
        }

    }

    void Write(net::WriteStream& s, const SubChunkRequestPacket& v) {
        Serialize(s, const_cast<SubChunkRequestPacket&>(v));
    }

    bool Read(net::ReadStream& s, SubChunkRequestPacket& v) {
        Serialize(s, v);
        return s.ok;
    }

}
