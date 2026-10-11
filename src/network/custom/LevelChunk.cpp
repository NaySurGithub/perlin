#include "network/custom/LevelChunk.hpp"

namespace perlin::proto {

    namespace {

        template <class S> void Serialize(S& s, LevelChunkPacket& v) {
            s.zigzag(v.x);
            s.zigzag(v.z);
            s.zigzag(v.dimension);
            s.varint(v.subChunkCount);
            s.boolean(v.hasRequestLimit);
            if (v.hasRequestLimit)
                s.zigzag(v.requestLimit);
            s.boolean(v.cacheEnabled);
            uint32_t hashes = static_cast<uint32_t>(v.blobHashes.size());
            s.varint(hashes);
            if constexpr (S::kReading) {
                if (!s.count(hashes))
                    return;
                v.blobHashes.assign(hashes, 0);
            }
            for (uint64_t& hash : v.blobHashes)
                s.le(hash);
            uint32_t length = static_cast<uint32_t>(v.payload.size());
            s.varint(length);
            if constexpr (S::kReading) {
                if (!s.length(length))
                    return;
            }
            s.bytes(v.payload, length);
            if constexpr (S::kReading) {
                if (s.more())
                    s.boolean(v.isClientBiomeUpdate);
            } else {
                s.boolean(v.isClientBiomeUpdate);
            }
        }

    }

    void Write(net::WriteStream& s, const LevelChunkPacket& v) {
        Serialize(s, const_cast<LevelChunkPacket&>(v));
    }

    bool Read(net::ReadStream& s, LevelChunkPacket& v) {
        v = LevelChunkPacket{};
        Serialize(s, v);
        return s.ok;
    }

}
