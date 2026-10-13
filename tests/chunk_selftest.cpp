#include "protocol/Packets.hpp"
#include "network/VarInt.hpp"
#include "world/level/chunk/ChunkCodec.hpp"

#include <cstdio>
#include <random>

using namespace perlin;
using namespace perlin::proto;

template <class P> static bool Decode(const uint8_t* data, size_t size, P& packet) {
    perlin::net::ReadStream stream(data, size);
    return Read(stream, packet);
}
static int fails = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                               \
            fails++;                                                                                         \
        }                                                                                                    \
    } while (0)

static ChunkCodec::CellIds MakeCells(int distinct, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<int32_t> vals;
    for (int i = 0; i < distinct; i++)
        vals.push_back((int32_t)rng());
    ChunkCodec::CellIds c;
    for (auto& v : c)
        v = vals[rng() % vals.size()];
    return c;
}

int main() {

    for (int distinct : {1, 2, 3, 4, 5, 9, 17, 40, 65, 200, 300}) {
        auto cells = MakeCells(distinct, 1000 + distinct);
        std::vector<uint8_t> buf;
        ChunkCodec::WriteSubChunk(buf, -4, cells);
        buf.push_back(0xAB);
        SubChunkData sc;
        size_t used = 0;
        const char* err = nullptr;
        CHECK(ChunkCodec::ParseSubChunk(buf.data(), buf.size(), sc, used, &err));
        CHECK(used == buf.size() - 1);
        CHECK(sc.version == 9 && sc.hasYIndex && sc.yIndex == -4 && sc.storages.size() == 1);
        ChunkCodec::CellIds back;
        CHECK(ChunkCodec::Expand(sc.storages[0], back));
        CHECK(back == cells);
    }

    for (auto [distinct, words] : {std::pair{5, 410}, {20, 683}, {40, 820}}) {
        auto cells = MakeCells(distinct, 7 + distinct);
        std::vector<uint8_t> b;
        ChunkCodec::WriteStorage(b, cells);
        PaletteStorage st;
        wire::Reader r(b.data(), b.size());
        SubChunkData sc;
        size_t used;
        std::vector<uint8_t> sub = {9, 1, 0};
        sub.insert(sub.end(), b.begin(), b.end());
        CHECK(ChunkCodec::ParseSubChunk(sub.data(), sub.size(), sc, used));
        CHECK((int)sc.storages[0].words.size() == words);
    }

    {
        auto cells = MakeCells(30, 5);
        std::vector<uint8_t> buf;
        ChunkCodec::WriteSubChunk(buf, 0, cells);
        for (size_t cut = 0; cut + 1 < buf.size(); cut += 37) {
            SubChunkData sc;
            size_t used;
            CHECK(!ChunkCodec::ParseSubChunk(buf.data(), cut, sc, used));
        }
    }

    {
        ChunkCodec::CellIds c;
        c.fill(0);
        c[1 * 256 + 2 * 16 + 3] = 77;
        std::vector<uint8_t> b;
        ChunkCodec::WriteSubChunk(b, 0, c);
        SubChunkData sc;
        size_t used;
        ChunkCodec::ParseSubChunk(b.data(), b.size(), sc, used);
        ChunkCodec::CellIds back;
        ChunkCodec::Expand(sc.storages[0], back);
        CHECK(back[291] == 77 && back[0] == 0);
    }

    {
        std::vector<uint8_t> b;
        wire::WriteZigzag32(b, -3);
        wire::WriteZigzag32(b, 12);
        wire::WriteZigzag32(b, 0);
        wire::WriteVarUint32(b, 2);
        b.push_back(0);
        b.push_back(0);
        wire::WriteVarUint32(b, 0);
        std::vector<uint8_t> payload = {1, 2, 3};
        wire::WriteVarUint32(b, (uint32_t)payload.size());
        b.insert(b.end(), payload.begin(), payload.end());
        LevelChunkPacket p;
        CHECK(Decode(b.data(), b.size(), p));
        CHECK(p.x == -3 && p.z == 12 && p.dimension == 0 && p.subChunkCount == 2);
        CHECK(p.GetMode() == LevelChunkPacket::Mode::Inline && p.RequestSlots(24) == 0);
        CHECK(p.payload == payload && !p.cacheEnabled);
    }
    {
        std::vector<uint8_t> b;
        wire::WriteZigzag32(b, 5);
        wire::WriteZigzag32(b, -6);
        wire::WriteZigzag32(b, 0);
        wire::WriteVarUint32(b, 0);
        b.push_back(1);
        wire::WriteZigzag32(b, 9);
        b.push_back(0);
        wire::WriteVarUint32(b, 0);
        wire::WriteVarUint32(b, 0);
        LevelChunkPacket p;
        CHECK(Decode(b.data(), b.size(), p));
        CHECK(p.GetMode() == LevelChunkPacket::Mode::Request && p.requestLimit == 9);
        CHECK(p.RequestSlots(24) == 10 && p.RequestSlots(8) == 8);
    }
    {
        std::vector<uint8_t> b;
        wire::WriteZigzag32(b, 0);
        wire::WriteZigzag32(b, 0);
        wire::WriteZigzag32(b, 0);
        wire::WriteVarUint32(b, 0);
        b.push_back(1);
        wire::WriteZigzag32(b, -1);
        b.push_back(1);
        wire::WriteVarUint32(b, 2);
        for (int i = 0; i < 4; i++)
            wire::WriteLE32(b, (uint32_t)(i + 1));
        wire::WriteVarUint32(b, 0);
        LevelChunkPacket p;
        CHECK(Decode(b.data(), b.size(), p));
        CHECK(p.GetMode() == LevelChunkPacket::Mode::Request && p.cacheEnabled);
        CHECK(p.blobHashes.size() == 2 && p.RequestSlots(24) == 24);
    }

    {
        auto cells = MakeCells(12, 99);
        std::vector<uint8_t> sub;
        ChunkCodec::WriteSubChunk(sub, 3, cells);
        std::vector<uint8_t> b;
        b.push_back(0);
        wire::WriteZigzag32(b, 0);
        wire::WriteLE32(b, 4);
        wire::WriteLE32(b, 0);
        wire::WriteLE32(b, (uint32_t)-2);
        wire::WriteVarUint32(b, 2);
        b.push_back(0);
        b.push_back((uint8_t)(int8_t)5);
        b.push_back(0);
        b.push_back(1);
        b.push_back(1);
        wire::WriteVarUint32(b, (uint32_t)sub.size());
        b.insert(b.end(), sub.begin(), sub.end());
        b.push_back(1);
        b.push_back(1);
        b.insert(b.end(), 256, 7);
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
        b.push_back(6);
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
        SubChunkPacket p;
        CHECK(Decode(b.data(), b.size(), p));
        CHECK(p.exact && p.x == 4 && p.z == -2 && p.entries.size() == 2);
        CHECK(p.entries[0].dy == 5 && p.entries[0].result == 1 && p.entries[0].payload == sub);
        CHECK(p.entries[1].result == (uint8_t)SubChunkResult::SuccessAllAir);
    }
    {
        auto req = perlin::net::Encode(SubChunkRequestPacket::ForSlot(0, -3, -4, 7));
        wire::Reader r(req.data(), req.size());
        int32_t d;
        uint32_t n, x, y, z;
        int8_t a, bb, c;
        CHECK(r.ReadZigzag32(d) && r.ReadVarUint32(n));
        CHECK(r.ReadInt8(a) && r.ReadInt8(bb) && r.ReadInt8(c));
        CHECK(r.ReadLE32(x) && r.ReadLE32(y) && r.ReadLE32(z));
        CHECK(d == 0 && n == 1 && a == 0 && bb == -4 && c == 0);
        CHECK((int32_t)x == -3 && y == 0 && z == 7);
        CHECK(r.Remaining() == 0);
    }
    printf(fails ? "%d FAILED\n" : "all chunk self-tests passed\n", fails);
    return fails ? 1 : 0;
}
