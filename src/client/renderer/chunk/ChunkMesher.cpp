#include <unordered_set>
#include <mutex>
#include "client/renderer/chunk/ChunkMesher.hpp"
#include "world/level/Level.hpp"
#include "client/renderer/block/BiomeTints.hpp"
#include "client/renderer/block/BlockLibrary.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/renderer/Tessellator.hpp"
#include "world/level/lighting/ChunkLighting.hpp"
#include "world/level/lighting/LightRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace perlin {
    static constexpr float kShadeTop = 1.0f;
    static constexpr float kShadeBottom = 0.5f;
    static constexpr float kShadeX = 0.6f;
    static constexpr float kShadeZ = 0.8f;
    static constexpr float kAoSolid = 0.2f;

    static bool TexHas(const std::string& tex, const char* sub) {
        return tex.find(sub) != std::string::npos;
    }
    static bool IsWaterTex(const std::string& t) {
        return TexHas(t, "still_water") || TexHas(t, "water_flow") || TexHas(t, "flowing_water") ||
               TexHas(t, "water_still");
    }
    static bool IsLavaTex(const std::string& t) {
        return TexHas(t, "lava_") || TexHas(t, "lava");
    }

    struct GeomInfo {
        bool slab = false;
        bool slabTop = false;
        bool dblPlant = false;
        bool upper = false;
        int snowH = -1;
        int liquidDepth = -1;
        int growth = -1;
        bool stair = false;
        int stairDir = 0;
        bool stairTop = false;
        int stairCorner = 0;
        bool fence = false;
        bool fenceArm[4] = {false, false, false, false};
        bool wall = false;
        int wallArm[4] = {0, 0, 0, 0};
        bool wallPost = false;
        int cakeBites = -1;
    };
    const GeomInfo& GeomFor(uint16_t id) {
        struct Slot {
            GeomInfo info;
            bool ready = false;
        };
        constexpr size_t kPageBits = 8;
        constexpr size_t kPageSize = size_t(1) << kPageBits;
        static thread_local std::vector<std::unique_ptr<Slot[]>> pages(65536 >> kPageBits);

        std::unique_ptr<Slot[]>& page = pages[id >> kPageBits];
        if (!page)
            page = std::make_unique<Slot[]>(kPageSize);
        Slot& slot = page[id & (kPageSize - 1)];
        if (slot.ready)
            return slot.info;

        GeomInfo g;
        {
            auto& pal = BlockPalette::Instance();
            if (pal.HasState(id, "minecraft:vertical_half")) {
                g.slab = true;
                g.slabTop = pal.StateString(id, "minecraft:vertical_half", "bottom") == "top";
            }
            if (pal.HasState(id, "upper_block_bit")) {
                g.dblPlant = true;
                g.upper = pal.StateInt(id, "upper_block_bit", 0) != 0;
            }
            if (pal.HasState(id, "height"))
                g.snowH = pal.StateInt(id, "height", -1);
            if (pal.HasState(id, "liquid_depth"))
                g.liquidDepth = pal.StateInt(id, "liquid_depth", 0);
            if (pal.HasState(id, "growth"))
                g.growth = pal.StateInt(id, "growth", -1);
            if (pal.HasState(id, "weirdo_direction")) {
                g.stair = true;
                g.stairDir = pal.StateInt(id, "weirdo_direction", 0) & 3;
                g.stairTop = pal.StateInt(id, "upside_down_bit", 0) != 0;
                std::string c = pal.StateString(id, "minecraft:corner", "none");
                g.stairCorner = (c == "inner_left")    ? 1
                                : (c == "inner_right") ? 2
                                : (c == "outer_left")  ? 3
                                : (c == "outer_right") ? 4
                                                       : 0;
            }
            if (pal.HasState(id, "minecraft:connection_north")) {
                g.fence = true;
                g.fenceArm[0] = pal.StateInt(id, "minecraft:connection_east", 0) != 0;
                g.fenceArm[1] = pal.StateInt(id, "minecraft:connection_west", 0) != 0;
                g.fenceArm[2] = pal.StateInt(id, "minecraft:connection_south", 0) != 0;
                g.fenceArm[3] = pal.StateInt(id, "minecraft:connection_north", 0) != 0;
            }
            if (pal.HasState(id, "wall_connection_type_east")) {
                g.wall = true;
                auto arm = [&](const char* k) {
                    std::string v = pal.StateString(id, k, "none");
                    return v == "tall" ? 2 : (v == "short" ? 1 : 0);
                };
                g.wallArm[0] = arm("wall_connection_type_east");
                g.wallArm[1] = arm("wall_connection_type_west");
                g.wallArm[2] = arm("wall_connection_type_south");
                g.wallArm[3] = arm("wall_connection_type_north");
                g.wallPost = pal.StateInt(id, "wall_post_bit", 0) != 0;
            }
            if (pal.HasState(id, "bite_counter"))
                g.cakeBites = pal.StateInt(id, "bite_counter", -1);
        }
        slot.info = g;
        slot.ready = true;
        return slot.info;
    }

    static uint32_t PosHash(int x, int y, int z) {
        uint32_t h = (uint32_t)(x * 374761393 + y * 668265263 + z * 1440662683u);
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    static void PlantOffset(int x, int z, const std::string& tex, bool doublePlant, float& dx, float& dy, float& dz) {
        dx = dy = dz = 0.0f;
        static const char* const names[] = {"grass", "fern", "flower", "poppy", "dandelion", "tulip", "orchid",
                                            "allium", "azure", "daisy", "cornflower", "lily_of", "rose", "peony",
                                            "lilac", "sunflower", "dead_bush", "deadbush", "double_plant"};
        bool offset = false;
        for (const char* name : names)
            if (tex.find(name) != std::string::npos)
                offset = true;
        if (!offset)
            return;
        int64_t seed = static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(x) * 3129871u)) ^
                       static_cast<int64_t>(z) * 116129781LL;
        seed = seed * seed * 42317861LL + seed * 11LL;
        seed >>= 16;
        dx = std::clamp((static_cast<float>(seed & 15) / 15.0f - 0.5f) * 0.5f, -0.25f, 0.25f);
        dz = std::clamp((static_cast<float>((seed >> 8) & 15) / 15.0f - 0.5f) * 0.5f, -0.25f, 0.25f);
        if (!doublePlant && (tex.find("tallgrass") != std::string::npos || tex.find("fern") != std::string::npos))
            dy = (static_cast<float>((seed >> 4) & 15) / 15.0f - 1.0f) * 0.2f;
    }

    static int PickTile(const std::vector<int>& variants, int fallback, uint32_t h) {
        if (variants.empty())
            return fallback;
        return variants[h % variants.size()];
    }

    struct CellLight {
        float block = 0;
        float sky = 15;
    };

    static void EmitBox(Tessellator& tess, const CellLight& light, float bx, float by, float bz, float x0,
                        float y0, float z0, float x1, float y1, float z1, int tTop, int tSide, int tBottom) {
        const float e = 0.002f;
        x0 += e;
        y0 += e;
        z0 += e;
        x1 -= e;
        y1 -= e;
        z1 -= e;
        struct BF {
            int axis;
            float dir;
            int tile;
            float shade;
        };
        const BF faces[6] = {{1, 1, tTop, kShadeTop}, {1, -1, tBottom, kShadeBottom},
                             {0, 1, tSide, kShadeX},  {0, -1, tSide, kShadeX},
                             {2, 1, tSide, kShadeZ},  {2, -1, tSide, kShadeZ}};
        for (auto [axis, dir, tile, shade] : faces) {
            if (!tile)
                continue;
            float u0, v0, u1, v1;
            BlockLibrary::TileUV(tile, u0, v0, u1, v1);
            float v[4][3];
            if (axis == 0) {
                float X = dir > 0 ? x1 : x0;
                v[0][0] = X;
                v[0][1] = y0;
                v[0][2] = z0;
                v[1][0] = X;
                v[1][1] = y0;
                v[1][2] = z1;
                v[2][0] = X;
                v[2][1] = y1;
                v[2][2] = z1;
                v[3][0] = X;
                v[3][1] = y1;
                v[3][2] = z0;
            } else if (axis == 1) {
                float Y = dir > 0 ? y1 : y0;
                v[0][0] = x0;
                v[0][1] = Y;
                v[0][2] = z0;
                v[1][0] = x1;
                v[1][1] = Y;
                v[1][2] = z0;
                v[2][0] = x1;
                v[2][1] = Y;
                v[2][2] = z1;
                v[3][0] = x0;
                v[3][1] = Y;
                v[3][2] = z1;
            } else {
                float Z = dir > 0 ? z1 : z0;
                v[0][0] = x0;
                v[0][1] = y0;
                v[0][2] = Z;
                v[1][0] = x1;
                v[1][1] = y0;
                v[1][2] = Z;
                v[2][0] = x1;
                v[2][1] = y1;
                v[2][2] = Z;
                v[3][0] = x0;
                v[3][1] = y1;
                v[3][2] = Z;
            }
            for (int i = 0; i < 4; i++) {
                v[i][0] += bx;
                v[i][1] += by;
                v[i][2] += bz;
            }
            float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            float col[4][3] = {
                {shade, shade, shade}, {shade, shade, shade}, {shade, shade, shade}, {shade, shade, shade}};
            tess.AddQuad(v, col, uv, false, light.block, light.sky);
        }
    }

    struct IdMemo {
        std::vector<const BlockAppearance*> app = std::vector<const BlockAppearance*>(65536, nullptr);
        std::vector<int8_t> solid = std::vector<int8_t>(65536, -1);
        std::vector<int8_t> water = std::vector<int8_t>(65536, -1);
        std::vector<int8_t> translucent = std::vector<int8_t>(65536, -1);
        std::vector<int8_t> ao = std::vector<int8_t>(65536, -1);
        std::vector<uint8_t> opacity = std::vector<uint8_t>(65536, 0);
        std::vector<uint8_t> opacityKnown = std::vector<uint8_t>(65536, 0);
        std::vector<uint32_t> stamp = std::vector<uint32_t>(65536, 0);
        uint32_t epoch = 1;

        void Reset() { epoch++; }
        void Touch(uint16_t id) {
            if (stamp[id] == epoch)
                return;
            stamp[id] = epoch;
            app[id] = nullptr;
            solid[id] = -1;
            water[id] = -1;
            translucent[id] = -1;
            ao[id] = -1;
            opacityKnown[id] = 0;
        }
    };
    static IdMemo& Memo() {
        static thread_local IdMemo m;
        return m;
    }
    static const BlockAppearance* LookupApp(BlockLibrary* library, uint16_t id) {
        IdMemo& m = Memo();
        m.Touch(id);
        const BlockAppearance*& slot = m.app[id];
        if (!slot)
            slot = &library->Look(id);
        return slot;
    }

    static bool IsLiquidId(BlockLibrary* library, uint16_t id) {
        IdMemo& m = Memo();
        m.Touch(id);
        int8_t& slot = m.water[id];
        if (slot < 0) {
            const BlockAppearance* a = library ? LookupApp(library, id) : nullptr;
            slot = a && (IsWaterTex(a->texTop) || IsWaterTex(a->texSide)) ? 1 : 0;
        }
        return slot != 0;
    }

    static bool IsTranslucentId(BlockLibrary* library, uint16_t id) {
        IdMemo& m = Memo();
        m.Touch(id);
        int8_t& slot = m.translucent[id];
        if (slot < 0) {
            slot = 0;
            if (library) {
                const BlockAppearance& a = *LookupApp(library, id);
                const int tiles[3] = {a.top, a.side, a.bottom};
                for (int tile : tiles)
                    if (tile && library->AlphaOf(tile).translucent)
                        slot = 1;
            }
        }
        return slot != 0;
    }

    int BlockLightOpacity(BlockLibrary& library, uint16_t id) {
        const BlockAppearance& a = library.Look(id);
        if ((a.top | a.side | a.bottom) == 0)
            return 0;
        if (a.shape != BlockAppearance::Shape::Cube)
            return 0;
        const GeomInfo& g = GeomFor(id);
        if (g.stair || g.slab)
            return kMaxLight;
        if (g.fence || g.wall || g.cakeBites >= 0)
            return 0;
        if (IsWaterTex(a.texTop) || IsWaterTex(a.texSide))
            return 1;
        const int tiles[3] = {a.top, a.side, a.bottom};
        for (int i = 0; i < 3; i++)
            if (tiles[i] && library.AlphaOf(tiles[i]).holes && !(i == 1 && a.overlayFrac > 0 && a.overlayTile))
                return 1;
        return kMaxLight;
    }

    struct TintKey {
        const void* app;
        int tile;
        size_t texHash;
        int biome;
        bool operator==(const TintKey& o) const {
            return app == o.app && tile == o.tile && texHash == o.texHash && biome == o.biome;
        }
    };
    struct TintKeyHash {
        size_t operator()(const TintKey& k) const {
            size_t h = std::hash<const void*>()(k.app);
            h = h * 1000003u ^ (size_t)k.tile;
            h = h * 1000003u ^ k.texHash;
            h = h * 1000003u ^ (size_t)k.biome;
            return h;
        }
    };

    struct BlockCache {
        std::vector<uint16_t> cells;
        std::vector<uint8_t> solid;
        std::vector<uint8_t> biome;
        int yBase = 0, yRows = 0;

        size_t Idx(int x, int y, int z) const {
            return ((size_t)(y - yBase) * 18 + (size_t)(z + 1)) * 18 + (size_t)(x + 1);
        }
        bool InRange(int x, int y, int z) const {
            return x >= -1 && x <= 16 && z >= -1 && z <= 16 && y >= yBase && y < yBase + yRows;
        }

        void FillRaw(Level& level, const ChunkPos& pos, int yLo, int yHi) {
            yBase = yLo;
            yRows = yHi - yLo + 1;
            const size_t n = (size_t)18 * (size_t)yRows * 18;
            cells.assign(n, 0);
            solid.assign(n, 0);
            biome.assign(n, 1);
            const Chunk* around[3][3];
            for (int dz = 0; dz < 3; dz++)
                for (int dx = 0; dx < 3; dx++) {
                    const auto it = level.chunks.find({pos.x + dx - 1, pos.z + dz - 1});
                    around[dz][dx] = it == level.chunks.end() ? nullptr : &it->second;
                }

            constexpr size_t kRowStride = 18 * 18;
            for (int z = -1; z <= 16; z++)
                for (int x = -1; x <= 16; x++) {
                    const Chunk* src = around[z < 0 ? 0 : (z > 15 ? 2 : 1)][x < 0 ? 0 : (x > 15 ? 2 : 1)];
                    if (!src)
                        continue;
                    const size_t base = Idx(x, yLo, z);
                    src->ReadColumn(x & 15, z & 15, yLo, yHi, &cells[base], &biome[base], kRowStride);
                }
        }

        void Classify(BlockLibrary* library) {
            IdMemo& m = Memo();
            const size_t n = cells.size();
            for (size_t ci = 0; ci < n; ci++) {
                uint16_t id = cells[ci];
                if (id == 0)
                    continue;
                m.Touch(id);
                int8_t sv = m.solid[id];
                if (sv < 0) {
                    sv = ComputeSolid(library, id) ? 1 : 0;
                    m.solid[id] = sv;
                }
                solid[ci] = (uint8_t)sv;
            }
        }

        static bool ComputeSolid(BlockLibrary* library, uint16_t id) {
            if (!library)
                return true;
            const BlockAppearance& a = *LookupApp(library, id);
            if ((a.top | a.side | a.bottom) == 0)
                return false;
            if (a.shape != BlockAppearance::Shape::Cube)
                return false;
            const GeomInfo& g = GeomFor(id);
            if (g.stair || g.fence || g.wall || g.cakeBites >= 0)
                return false;
            BlockLibrary::TileAlpha at = library->AlphaOf(a.top);
            BlockLibrary::TileAlpha as = library->AlphaOf(a.side);
            BlockLibrary::TileAlpha ab = library->AlphaOf(a.bottom);
            // The grass-block side tile is synthesised with transparent holes under the grass overlay;
            // that must not make the whole block count as see-through.
            // Only opaque cubes occlude: water, ice, glass and other translucent tiles must not cull the
            // faces of the blocks around them (the terrain next to water was missing faces).
            if ((a.top && at.translucent) || (a.side && as.translucent) || (a.bottom && ab.translucent))
                return false;
            if (IsWaterTex(a.texTop) || IsWaterTex(a.texSide))
                return false;
            const bool sideHoles = a.side && as.holes && !(a.overlayFrac > 0 && a.overlayTile);
            bool holes = (a.top && at.holes) || sideHoles || (a.bottom && ab.holes);
            return !holes;
        }

        uint16_t Get(int x, int y, int z) const {
            return InRange(x, y, z) ? cells[Idx(x, y, z)] : (uint16_t)0;
        }
        uint8_t Occ(int x, int y, int z) const { return InRange(x, y, z) ? solid[Idx(x, y, z)] : (uint8_t)0; }
        uint8_t Biome(int x, int y, int z) const {
            return InRange(x, y, z) ? biome[Idx(x, y, z)] : (uint8_t)1;
        }
    };

    static float WaterCornerHeight(const BlockCache& cache, BlockLibrary* library, int y, int cx, int cz) {
        float total = 0.0f;
        int weight = 0;
        for (int i = 0; i < 4; i++) {
            const int px = cx - 1 + (i & 1), pz = cz - 1 + (i >> 1);
            if (IsLiquidId(library, cache.Get(px, y + 1, pz)))
                return 1.0f;
            const uint16_t id = cache.Get(px, y, pz);
            if (id != 0 && IsLiquidId(library, id)) {
                const int depth = GeomFor(id).liquidDepth;
                const float height = static_cast<float>(8 - (depth >= 0 ? (depth & 7) : 0)) / 9.0f;
                if (height >= 0.8f) {
                    total += height * 10.0f;
                    weight += 10;
                } else {
                    total += height;
                    weight++;
                }
            } else if (!cache.Occ(px, y, pz)) {
                weight++;
            }
        }
        return weight ? total / static_cast<float>(weight) : 0.0f;
    }

    // BlockBehaviour.getShadeBrightness: 0.2 for full-collision blocks, 1.0 otherwise (glass, water,
    // plants, slabs...). Leaves and ice are full collision blocks that Perlin does not mark opaque.
    static float AoShade(const BlockCache& cache, BlockLibrary* library, int x, int y, int z) {
        const uint16_t id = cache.Get(x, y, z);
        if (id == 0)
            return 1.0f;
        if (cache.Occ(x, y, z))
            return 0.2f;
        IdMemo& m = Memo();
        m.Touch(id);
        if (m.ao[id] < 0) {
            const std::string name = BlockPalette::Instance().Name(id);
            const BlockAppearance* a = library ? LookupApp(library, id) : nullptr;
            const bool cube = a && a->shape == BlockAppearance::Shape::Cube && (a->top | a->side | a->bottom) != 0;
            m.ao[id] = cube && (name.find("leaves") != std::string::npos || name == "minecraft:ice" ||
                                name == "minecraft:frosted_ice") &&
                               !IsLiquidId(library, id)
                           ? 1
                           : 0;
        }
        return m.ao[id] == 1 ? 0.2f : 1.0f;
    }

    void BuildChunkMesh(Level& level, const ChunkPos& pos, Tessellator& tess, BlockLibrary* library,
                        BiomeTints* tints) {
        bool textured = library && library->Ready();
        bool haveTints = tints && tints->Ready();

        static thread_local BlockCache cache;
        int yLo = 0, yHi = 0;
        {
            std::lock_guard<std::mutex> lk(level.mutex);
            auto it = level.chunks.find(pos);
            if (it == level.chunks.end())
                return;
            if (!it->second.YRange(yLo, yHi))
                return;
            int fLo = std::max(yLo - 1, (int)Chunk::MIN_Y);
            int fHi = std::min(yHi + 1, (int)Chunk::MIN_Y + Chunk::HEIGHT - 1);
            cache.FillRaw(level, pos, fLo, fHi);
        }
        Memo().Reset();
        if (library)
            cache.Classify(library);
        else
            for (size_t i = 0; i < cache.cells.size(); i++)
                cache.solid[i] = cache.cells[i] != 0;
        static thread_local ChunkLighting::Result lighting;
        ChunkLighting::Compute(level, pos, cache.yBase, cache.yBase + cache.yRows - 1, lighting);
        const LightRegistry& registry = LightRegistry::Instance();
        auto sample = [&](int lx, int ly, int lz) {
            const uint8_t packed = lighting.At(lx, ly, lz);
            return std::array<int, 2>{ChunkLighting::Result::Block(packed),
                                      ChunkLighting::Result::Sky(packed)};
        };
        auto ownLight = [&](int lx, int ly, int lz, uint16_t cellId) {
            std::array<int, 2> l = sample(lx, ly, lz);
            if (!cache.Occ(lx, ly, lz) && registry.Resolve(cellId).opacity >= kMaxLight) {
                static constexpr int kOffsets[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                                       {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
                for (const auto& o : kOffsets) {
                    const std::array<int, 2> n = sample(lx + o[0], ly + o[1], lz + o[2]);
                    l[0] = std::max(l[0], n[0]);
                    l[1] = std::max(l[1], n[1]);
                }
            }
            return CellLight{static_cast<float>(l[0]), static_cast<float>(l[1])};
        };
        tess.Reserve(8192);

        std::vector<std::string> biomeNames(256);
        std::unordered_map<TintKey, std::array<float, 4>, TintKeyHash> tintMemo;
        std::unordered_map<TintKey, std::array<float, 4>, TintKeyHash> blendMemo;
        static const std::string kPlainsName = "minecraft:plains";

        int baseX = pos.x * 16, baseZ = pos.z * 16;
        for (int y = yLo; y <= yHi; y++) {
            tess.EnterSection(y >> 4);
            for (int z = 0; z < 16; z++)
                for (int x = 0; x < 16; x++) {
                    uint16_t id = cache.Get(x, y, z);
                    if (id == 0)
                        continue;
                    tess.SetLayer(RenderLayer::Cutout);
                    const CellLight cellLight = ownLight(x, y, z, id);
                    const BlockAppearance* app = nullptr;
                    if (textured)
                        app = LookupApp(library, id);
                    // Blocks without any texture (unknown hashes, barrier, light blocks, structure void...)
                    // are invisible in Java; drawing them as a stand-in cube produced phantom blocks.
                    if (app && (app->top | app->side | app->bottom) == 0) {
                        static std::mutex logMutex;
                        static std::unordered_set<uint16_t> logged;
                        std::lock_guard<std::mutex> lk(logMutex);
                        if (logged.insert(id).second)
                            printf("[blocks] hidden untextured block id=%u name='%s' at (%d,%d,%d)\n",
                                   (unsigned)id, BlockPalette::Instance().Name(id).c_str(), baseX + x, y,
                                   baseZ + z);
                        continue;
                    }
                    float bx = (float)(baseX + x), by = (float)y, bz = (float)(baseZ + z);

                    const int biomeId = cache.Biome(x, y, z);
                    const std::string* biomeNamePtr = &kPlainsName;
                    if (haveTints) {
                        std::string& bn = biomeNames[(size_t)biomeId & 255];
                        if (bn.empty())
                            bn = tints->NameForId(biomeId);
                        biomeNamePtr = &bn;
                    }
                    const std::string& biomeName = *biomeNamePtr;
                    const GeomInfo& gi = GeomFor(id);
                    uint32_t bhash = PosHash(baseX + x, y, baseZ + z);

                    if (app && (gi.stair || gi.fence || gi.wall || gi.cakeBites >= 0)) {
                        int ct = PickTile(app->varTop, app->top, bhash);
                        int cs = PickTile(app->varSide, app->side, bhash);
                        int cb = PickTile(app->varBottom, app->bottom, bhash);
                        auto rotXZ = [&](float lx, float lz, int dir, float& ox, float& oz) {
                            switch (dir & 3) {
                            case 0:
                                ox = lx;
                                oz = lz;
                                break;
                            case 1:
                                ox = 1 - lx;
                                oz = 1 - lz;
                                break;
                            case 2:
                                ox = 1 - lz;
                                oz = lx;
                                break;
                            default:
                                ox = lz;
                                oz = 1 - lx;
                                break;
                            }
                        };
                        auto emitRot = [&](float lx0, float ly0, float lz0, float lx1, float ly1, float lz1,
                                           int dir, bool flipY) {
                            float ax0 = lx0, az0 = lz0, ax1 = lx1, az1 = lz1;
                            float ox0, oz0, ox1, oz1;
                            rotXZ(ax0, az0, dir, ox0, oz0);
                            rotXZ(ax1, az1, dir, ox1, oz1);
                            float rx0 = ox0 < ox1 ? ox0 : ox1, rx1 = ox0 < ox1 ? ox1 : ox0;
                            float rz0 = oz0 < oz1 ? oz0 : oz1, rz1 = oz0 < oz1 ? oz1 : oz0;
                            float ry0 = flipY ? 1 - ly1 : ly0, ry1 = flipY ? 1 - ly0 : ly1;
                            EmitBox(tess, cellLight, bx, by, bz, rx0, ry0, rz0, rx1, ry1, rz1, ct, cs, cb);
                        };
                        if (gi.stair) {
                            float yA0 = gi.stairTop ? 0.5f : 0.0f;
                            float yA1 = gi.stairTop ? 1.0f : 0.5f;
                            float yB0 = gi.stairTop ? 0.0f : 0.5f;
                            float yB1 = gi.stairTop ? 0.5f : 1.0f;
                            emitRot(0, yA0, 0, 1, yA1, 1, gi.stairDir, false);
                            switch (gi.stairCorner) {
                            case 1:
                                emitRot(0.5f, yB0, 0, 1, yB1, 1, gi.stairDir, false);
                                emitRot(0, yB0, 0, 0.5f, yB1, 0.5f, gi.stairDir, false);
                                break;
                            case 2:
                                emitRot(0.5f, yB0, 0, 1, yB1, 1, gi.stairDir, false);
                                emitRot(0, yB0, 0.5f, 0.5f, yB1, 1, gi.stairDir, false);
                                break;
                            case 3:
                                emitRot(0.5f, yB0, 0, 1, yB1, 0.5f, gi.stairDir, false);
                                break;
                            case 4:
                                emitRot(0.5f, yB0, 0.5f, 1, yB1, 1, gi.stairDir, false);
                                break;
                            default:
                                emitRot(0.5f, yB0, 0, 1, yB1, 1, gi.stairDir, false);
                                break;
                            }
                        } else if (gi.fence) {
                            EmitBox(tess, cellLight, bx, by, bz, 6 / 16.0f, 0, 6 / 16.0f, 10 / 16.0f, 1,
                                    10 / 16.0f, ct, cs, cb);
                            const float armY0 = 11 / 16.0f, armY1 = 15 / 16.0f;
                            if (gi.fenceArm[0])
                                EmitBox(tess, cellLight, bx, by, bz, 10 / 16.0f, armY0, 6 / 16.0f, 1, armY1,
                                        10 / 16.0f, ct, cs, cb);
                            if (gi.fenceArm[1])
                                EmitBox(tess, cellLight, bx, by, bz, 0, armY0, 6 / 16.0f, 6 / 16.0f, armY1,
                                        10 / 16.0f, ct, cs, cb);
                            if (gi.fenceArm[2])
                                EmitBox(tess, cellLight, bx, by, bz, 6 / 16.0f, armY0, 10 / 16.0f, 1, armY1,
                                        10 / 16.0f, ct, cs, cb);
                            if (gi.fenceArm[3])
                                EmitBox(tess, cellLight, bx, by, bz, 6 / 16.0f, armY0, 0, 10 / 16.0f, armY1,
                                        6 / 16.0f, ct, cs, cb);
                        } else if (gi.wall) {
                            EmitBox(tess, cellLight, bx, by, bz, 5 / 16.0f, 0, 5 / 16.0f, 11 / 16.0f, 1,
                                    11 / 16.0f, ct, cs, cb);
                            const int h[4] = {gi.wallArm[0] >= 2 ? 16 : 14, gi.wallArm[1] >= 2 ? 16 : 14,
                                              gi.wallArm[2] >= 2 ? 16 : 14, gi.wallArm[3] >= 2 ? 16 : 14};
                            if (gi.wallArm[0])
                                EmitBox(tess, cellLight, bx, by, bz, 11 / 16.0f, 0, 5 / 16.0f, 1,
                                        h[0] / 16.0f, 11 / 16.0f, ct, cs, cb);
                            if (gi.wallArm[1])
                                EmitBox(tess, cellLight, bx, by, bz, 0, 0, 5 / 16.0f, 5 / 16.0f, h[1] / 16.0f,
                                        11 / 16.0f, ct, cs, cb);
                            if (gi.wallArm[2])
                                EmitBox(tess, cellLight, bx, by, bz, 5 / 16.0f, 0, 11 / 16.0f, 1,
                                        h[2] / 16.0f, 11 / 16.0f, ct, cs, cb);
                            if (gi.wallArm[3])
                                EmitBox(tess, cellLight, bx, by, bz, 5 / 16.0f, 0, 0, 11 / 16.0f,
                                        h[3] / 16.0f, 11 / 16.0f, ct, cs, cb);
                        } else {
                            float x0 = (1 + 2 * gi.cakeBites) / 16.0f;
                            if (x0 < 0)
                                x0 = 0;
                            if (x0 > 15 / 16.0f)
                                x0 = 15 / 16.0f;
                            EmitBox(tess, cellLight, bx, by, bz, x0, 0, 1 / 16.0f, 15 / 16.0f, 8 / 16.0f,
                                    15 / 16.0f, ct, cs, cb);
                        }
                        continue;
                    }

                    auto biomeTintForRaw = [&](const std::string& biomeName, const std::string& tex, int tile,
                                               bool isTop) -> std::array<float, 4> {
                        (void)isTop;
                        if (app) {
                            static const char* kGrassKeys[] = {
                                "grass_top",   "grass_carried",      "tallgrass", "tall_grass",
                                "short_grass", "double_plant_grass", "fern",      "large_fern"};
                            std::string tpath = library ? library->TilePathOf(tile) : std::string();
                            bool grassLike = false;
                            for (const char* k : kGrassKeys)
                                if (TexHas(tex, k) || TexHas(tpath, k)) {
                                    grassLike = true;
                                    break;
                                }
                            if (grassLike) {
                                std::array<float, 3> c =
                                    haveTints ? tints->Grass(biomeName)
                                              : std::array<float, 3>{0.4745f, 0.7529f, 0.3529f};
                                static int logged = 0;
                                if (logged < 6) {
                                    logged++;
                                    printf("[tint] grass-like tex='%s' path='%s' tile=%d haveTints=%d "
                                           "biome=%s -> (%.2f,%.2f,%.2f)\n",
                                           tex.c_str(), tpath.c_str(), tile, (int)haveTints,
                                           biomeName.c_str(), c[0], c[1], c[2]);
                                }
                                return {c[0], c[1], c[2], 1};
                            }
                        }
                        if (!haveTints || !app)
                            return {app ? app->tintR : 1, app ? app->tintG : 1, app ? app->tintB : 1, 0};
                        if (IsWaterTex(tex)) {
                            auto c = tints->Water(biomeName);
                            return {c[0], c[1], c[2], 1};
                        }
                        if (IsLavaTex(tex))
                            return {1, 1, 1, 1};
                        if (library && library->SaturationOf(tile) > 0.25f)
                            return {1, 1, 1, 1};
                        if (TexHas(tex, "leaves") || TexHas(tex, "leaf")) {
                            if (TexHas(tex, "birch"))
                                return {tints->MapAverage("birch")[0], tints->MapAverage("birch")[1],
                                        tints->MapAverage("birch")[2], 1};
                            if (TexHas(tex, "spruce") || TexHas(tex, "evergreen"))
                                return {tints->MapAverage("evergreen")[0], tints->MapAverage("evergreen")[1],
                                        tints->MapAverage("evergreen")[2], 1};
                            if (TexHas(tex, "mangrove"))
                                return {tints->MapAverage("mangrove_swamp_foliage")[0],
                                        tints->MapAverage("mangrove_swamp_foliage")[1],
                                        tints->MapAverage("mangrove_swamp_foliage")[2], 1};
                            auto c = tints->Foliage(biomeName);
                            return {c[0], c[1], c[2], 1};
                        }
                        if (TexHas(tex, "grass_carried") || tex == "grass_top" || TexHas(tex, "grass_top") ||
                            TexHas(tex, "tallgrass") || TexHas(tex, "tall_grass") ||
                            TexHas(tex, "short_grass") || TexHas(tex, "double_plant_grass") ||
                            tex == "fern" || TexHas(tex, "fern_carried")) {
                            auto c = tints->Grass(biomeName);
                            return {c[0], c[1], c[2], 1};
                        }
                        if (app->useCarriedColor) {
                            auto c = tints->Foliage(biomeName);
                            return {c[0], c[1], c[2], 1};
                        }
                        return {app->tintR, app->tintG, app->tintB, 0};
                    };
                    auto biomeTintFor = [&](const std::string& tex, int tile,
                                            bool isTop) -> std::array<float, 4> {
                        const size_t texHash = std::hash<std::string>()(tex);
                        TintKey blendKey{app, tile, texHash, (x & 31) | ((z & 31) << 5) | (((y - cache.yBase) & 1023) << 10)};
                        auto bit = blendMemo.find(blendKey);
                        if (bit != blendMemo.end())
                            return bit->second;
                        std::array<float, 4> sum{0, 0, 0, 0};
                        std::array<float, 4> own{0, 0, 0, 0};
                        int count = 0;
                        for (int dz = -2; dz <= 2; dz++)
                            for (int dx = -2; dx <= 2; dx++) {
                                const int sx = std::clamp(x + dx, -1, 16), sz = std::clamp(z + dz, -1, 16);
                                const int sample = cache.Biome(sx, y, sz);
                                TintKey k{app, tile, texHash, sample};
                                auto mit = tintMemo.find(k);
                                if (mit == tintMemo.end()) {
                                    std::string& bn = biomeNames[(size_t)sample & 255];
                                    if (bn.empty() && haveTints)
                                        bn = tints->NameForId(sample);
                                    mit = tintMemo.emplace(k, biomeTintForRaw(haveTints ? bn : kPlainsName, tex, tile, isTop)).first;
                                }
                                for (int c = 0; c < 3; c++)
                                    sum[c] += mit->second[c];
                                if (dx == 0 && dz == 0)
                                    own = mit->second;
                                count++;
                            }
                        std::array<float, 4> r = {sum[0] / count, sum[1] / count, sum[2] / count, own[3]};
                        blendMemo.emplace(blendKey, r);
                        return r;
                    };

                    if (app && app->shape == BlockAppearance::Shape::Flat) {
                        const std::vector<int>& fv = app->varTop.empty() ? app->varSide : app->varTop;
                        int tile = PickTile(fv, app->top ? app->top : app->side, bhash);
                        float baseRGB[3];
                        bool maskGrey = !library || library->SaturationOf(tile) < 0.25f;
                        if (app->hasCarriedRGB && maskGrey) {
                            baseRGB[0] = app->carriedR;
                            baseRGB[1] = app->carriedG;
                            baseRGB[2] = app->carriedB;
                        } else {
                            auto tc =
                                biomeTintFor(app->texTop.empty() ? app->texSide : app->texTop, tile, true);
                            baseRGB[0] = tc[0];
                            baseRGB[1] = tc[1];
                            baseRGB[2] = tc[2];
                            if (!tc[3]) {
                                if (app->tintTop) {
                                    baseRGB[0] = app->tintR;
                                    baseRGB[1] = app->tintG;
                                    baseRGB[2] = app->tintB;
                                } else {
                                    baseRGB[0] = baseRGB[1] = baseRGB[2] = 1.0f;
                                }
                            }
                        }
                        float u0, v0uv, u1, v1uv;
                        BlockLibrary::TileUV(tile, u0, v0uv, u1, v1uv);
                        float h = 0.0625f;
                        if (gi.snowH >= 0)
                            h = (gi.snowH + 1) * 2.0f / 16.0f;
                        else if (app->texTop.find("snow") != std::string::npos ||
                                 app->texSide.find("snow") != std::string::npos)
                            h = 0.125f;
                        float inset = 0;
                        if (gi.growth >= 0) {
                            float cov = 0.5f + 0.5f * (gi.growth + 1) / 8.0f;
                            inset = (1.0f - cov) * 0.5f;
                        }
                        float fx0 = bx + inset, fx1 = bx + 1 - inset;
                        float fz0 = bz + inset, fz1 = bz + 1 - inset;
                        float top[4][3] = {
                            {fx0, by + h, fz0}, {fx1, by + h, fz0}, {fx1, by + h, fz1}, {fx0, by + h, fz1}};
                        float uv[4][2] = {{u0, v0uv}, {u1, v0uv}, {u1, v1uv}, {u0, v1uv}};
                        {
                            int rot = (bhash >> 2) & 3;
                            float base[4][2] = {{u0, v0uv}, {u1, v0uv}, {u1, v1uv}, {u0, v1uv}};
                            for (int i = 0; i < 4; i++) {
                                uv[i][0] = base[(i + rot) & 3][0];
                                uv[i][1] = base[(i + rot) & 3][1];
                            }
                        }
                        float col[4][3];
                        for (int i = 0; i < 4; i++) {
                            col[i][0] = baseRGB[0] * kShadeTop;
                            col[i][1] = baseRGB[1] * kShadeTop;
                            col[i][2] = baseRGB[2] * kShadeTop;
                        }
                        tess.AddQuad(top, col, uv, false, cellLight.block, cellLight.sky);
                        float sideUv[4][2] = {{u0, v1uv}, {u1, v1uv}, {u1, v1uv}, {u0, v1uv}};
                        (void)sideUv;
                        float px[4][3] = {
                            {fx1, by + h, fz0}, {fx1, by, fz0}, {fx1, by, fz1}, {fx1, by + h, fz1}};
                        float nx[4][3] = {
                            {fx0, by + h, fz1}, {fx0, by, fz1}, {fx0, by, fz0}, {fx0, by + h, fz0}};
                        float pz[4][3] = {
                            {fx0, by + h, fz1}, {fx0, by, fz1}, {fx1, by, fz1}, {fx1, by + h, fz1}};
                        float nz[4][3] = {
                            {fx1, by + h, fz0}, {fx1, by, fz0}, {fx0, by, fz0}, {fx0, by + h, fz0}};
                        float scol[4][3];
                        for (int i = 0; i < 4; i++) {
                            scol[i][0] = baseRGB[0] * kShadeX;
                            scol[i][1] = baseRGB[1] * kShadeX;
                            scol[i][2] = baseRGB[2] * kShadeX;
                        }
                        float uvS[4][2] = {{u0, v0uv}, {u0, v0uv}, {u1, v0uv}, {u1, v0uv}};
                        if (cache.Get(x + 1, y, z) == 0)
                            tess.AddQuad(px, scol, uvS, false, cellLight.block, cellLight.sky);
                        if (cache.Get(x - 1, y, z) == 0)
                            tess.AddQuad(nx, scol, uvS, false, cellLight.block, cellLight.sky);
                        if (cache.Get(x, y, z + 1) == 0)
                            tess.AddQuad(pz, scol, uvS, false, cellLight.block, cellLight.sky);
                        if (cache.Get(x, y, z - 1) == 0)
                            tess.AddQuad(nz, scol, uvS, false, cellLight.block, cellLight.sky);
                        continue;
                    }

                    if (app && app->shape == BlockAppearance::Shape::Wall) {
                        int tile = app->side ? app->side : app->top;
                        std::string tex = app->texSide.empty() ? app->texTop : app->texSide;
                        auto tc = biomeTintFor(tex, tile, false);
                        float baseRGB[3] = {tc[0], tc[1], tc[2]};
                        if (!tc[3]) {
                            if (app->tintSide) {
                                baseRGB[0] = app->tintR;
                                baseRGB[1] = app->tintG;
                                baseRGB[2] = app->tintB;
                            } else {
                                baseRGB[0] = baseRGB[1] = baseRGB[2] = 1.0f;
                            }
                        }
                        float u0, v0uv, u1, v1uv;
                        BlockLibrary::TileUV(tile, u0, v0uv, u1, v1uv);
                        float uvw[4][2] = {{u0, v1uv}, {u0, v0uv}, {u1, v0uv}, {u1, v1uv}};
                        float colW[4][3];
                        for (int i = 0; i < 4; i++) {
                            colW[i][0] = baseRGB[0];
                            colW[i][1] = baseRGB[1];
                            colW[i][2] = baseRGB[2];
                        }
                        constexpr float kInset = 1.0f / 16.0f;
                        int quads = 0;
                        if (cache.Occ(x + 1, y, z)) {
                            float q[4][3] = {{bx + 1 - kInset, by, bz},
                                             {bx + 1 - kInset, by + 1, bz},
                                             {bx + 1 - kInset, by + 1, bz + 1},
                                             {bx + 1 - kInset, by, bz + 1}};
                            float c[4][3];
                            for (int i = 0; i < 4; i++) {
                                c[i][0] = colW[i][0] * kShadeX;
                                c[i][1] = colW[i][1] * kShadeX;
                                c[i][2] = colW[i][2] * kShadeX;
                            }
                            tess.AddQuad(q, c, uvw, false, cellLight.block, cellLight.sky);
                            quads++;
                        }
                        if (cache.Occ(x - 1, y, z)) {
                            float q[4][3] = {{bx + kInset, by, bz + 1},
                                             {bx + kInset, by + 1, bz + 1},
                                             {bx + kInset, by + 1, bz},
                                             {bx + kInset, by, bz}};
                            float c[4][3];
                            for (int i = 0; i < 4; i++) {
                                c[i][0] = colW[i][0] * kShadeX;
                                c[i][1] = colW[i][1] * kShadeX;
                                c[i][2] = colW[i][2] * kShadeX;
                            }
                            tess.AddQuad(q, c, uvw, false, cellLight.block, cellLight.sky);
                            quads++;
                        }
                        if (cache.Occ(x, y, z + 1)) {
                            float q[4][3] = {{bx + 1, by, bz + 1 - kInset},
                                             {bx + 1, by + 1, bz + 1 - kInset},
                                             {bx, by + 1, bz + 1 - kInset},
                                             {bx, by, bz + 1 - kInset}};
                            float c[4][3];
                            for (int i = 0; i < 4; i++) {
                                c[i][0] = colW[i][0] * kShadeZ;
                                c[i][1] = colW[i][1] * kShadeZ;
                                c[i][2] = colW[i][2] * kShadeZ;
                            }
                            tess.AddQuad(q, c, uvw, false, cellLight.block, cellLight.sky);
                            quads++;
                        }
                        if (cache.Occ(x, y, z - 1)) {
                            float q[4][3] = {{bx, by, bz + kInset},
                                             {bx, by + 1, bz + kInset},
                                             {bx + 1, by + 1, bz + kInset},
                                             {bx + 1, by, bz + kInset}};
                            float c[4][3];
                            for (int i = 0; i < 4; i++) {
                                c[i][0] = colW[i][0] * kShadeZ;
                                c[i][1] = colW[i][1] * kShadeZ;
                                c[i][2] = colW[i][2] * kShadeZ;
                            }
                            tess.AddQuad(q, c, uvw, false, cellLight.block, cellLight.sky);
                            quads++;
                        }
                        if (quads == 0) {
                            float va[4][3] = {{bx, by, bz + 1},
                                              {bx, by + 1, bz + 1},
                                              {bx + 1, by + 1, bz},
                                              {bx + 1, by, bz}};
                            float vb[4][3] = {{bx, by, bz},
                                              {bx, by + 1, bz},
                                              {bx + 1, by + 1, bz + 1},
                                              {bx + 1, by, bz + 1}};
                            tess.AddQuad(va, colW, uvw, false, cellLight.block, cellLight.sky);
                            tess.AddQuad(vb, colW, uvw, false, cellLight.block, cellLight.sky);
                        }
                        continue;
                    }

                    if (app && app->shape == BlockAppearance::Shape::Cross) {
                        const std::vector<int>& cvv = app->varSide.empty() ? app->varTop : app->varSide;
                        int tile = PickTile(cvv, app->side ? app->side : app->top, bhash);
                        std::string tex = app->texSide.empty() ? app->texTop : app->texSide;
                        if (gi.dblPlant) {
                            if (gi.upper && app->top) {
                                tile = PickTile(app->varTop, app->top, bhash);
                                tex = app->texTop;
                            }
                        }
                        auto tc = biomeTintFor(tex, tile, true);
                        float baseRGB[3] = {tc[0], tc[1], tc[2]};
                        if (!tc[3]) {
                            if (app->tintTop) {
                                baseRGB[0] = app->tintR;
                                baseRGB[1] = app->tintG;
                                baseRGB[2] = app->tintB;
                            } else {
                                baseRGB[0] = baseRGB[1] = baseRGB[2] = 1.0f;
                            }
                        }
                        float u0, v0uv, u1, v1uv;
                        BlockLibrary::TileUV(tile, u0, v0uv, u1, v1uv);
                        if ((bhash >> 3) & 1) {
                            float t = u0;
                            u0 = u1;
                            u1 = t;
                        }
                        float uvc[4][2] = {{u0, v1uv}, {u0, v0uv}, {u1, v0uv}, {u1, v1uv}};
                        float colC[4][3];
                        for (int i = 0; i < 4; i++) {
                            colC[i][0] = baseRGB[0];
                            colC[i][1] = baseRGB[1];
                            colC[i][2] = baseRGB[2];
                        }
                        float ox, oy, oz;
                        PlantOffset(baseX + x, baseZ + z, tex, gi.dblPlant, ox, oy, oz);
                        // Java tinted_cross uses rescale:true, so the 45-degree planes span 0.05..0.95 on both axes.
                        const float lo = 0.05f, hi = 0.95f;
                        const float px = bx + ox, py = by + oy, pz = bz + oz;
                        float va[4][3] = {
                            {px + lo, py, pz + hi}, {px + lo, py + 1, pz + hi}, {px + hi, py + 1, pz + lo}, {px + hi, py, pz + lo}};
                        float vb[4][3] = {
                            {px + lo, py, pz + lo}, {px + lo, py + 1, pz + lo}, {px + hi, py + 1, pz + hi}, {px + hi, py, pz + hi}};
                        tess.AddQuad(va, colC, uvc, false, cellLight.block, cellLight.sky);
                        tess.AddQuad(vb, colC, uvc, false, cellLight.block, cellLight.sky);
                        continue;
                    }

                    const bool isWater = app && (IsWaterTex(app->texTop) || IsWaterTex(app->texSide));
                    const bool translucent = isWater || (textured && IsTranslucentId(library, id));
                    int ovTile = 0;
                    float ovR = 1, ovG = 1, ovB = 1;
                    auto face = [&](int nx, int ny, int nz, int tile, float tintR, float tintG, float tintB,
                                    bool tinted, bool known, float shade, const int c[4][3],
                                    const float v[4][3]) {
                        const uint16_t neighborId = cache.Get(x + nx, y + ny, z + nz);
                        if (neighborId != 0 && cache.Occ(x + nx, y + ny, z + nz))
                            return;
                        if (neighborId != 0 && translucent &&
                            (neighborId == id || (isWater && IsLiquidId(library, neighborId))))
                            return;
                        float u0, v0uv, u1, v1uv;
                        BlockLibrary::TileUV(tile, u0, v0uv, u1, v1uv);
                        float uv[4][2];
                        float fuA[4], fvA[4];
                        for (int i = 0; i < 4; i++) {
                            float fu, fv;
                            if (nx != 0) {
                                fu = (float)(c[i][2] - z);
                                if (nx > 0)
                                    fu = 1.0f - fu;
                                fv = 1.0f - (float)(c[i][1] - y);
                            } else if (nz != 0) {
                                fu = (float)(c[i][0] - x);
                                if (nz < 0)
                                    fu = 1.0f - fu;
                                fv = 1.0f - (float)(c[i][1] - y);
                            } else {
                                fu = (float)(c[i][0] - x);
                                fv = (ny > 0) ? (float)(c[i][2] - z) : 1.0f - (float)(c[i][2] - z);
                            }
                            fuA[i] = fu;
                            fvA[i] = fv;
                            uv[i][0] = u0 + fu * (u1 - u0);
                            uv[i][1] = v0uv + fv * (v1uv - v0uv);
                        }
                        float base[3];
                        if (!textured) {
                            base[0] = 0.6f;
                            base[1] = 0.6f;
                            base[2] = 0.6f;
                        } else if (!known) {
                            base[0] = base[1] = base[2] = 0.6f;
                        } else if (tinted) {
                            base[0] = tintR;
                            base[1] = tintG;
                            base[2] = tintB;
                        } else {
                            base[0] = base[1] = base[2] = 1.0f;
                        }
                        float col[4][3];
                        float lt[4][2];
                        float kv[4];
                        float aoV[4];
                        const int origin[3] = {x, y, z};
                        const int center[3] = {x + nx, y + ny, z + nz};
                        const int axis1 = nx != 0 ? 1 : 0;
                        const int axis2 = nz != 0 ? 1 : 2;
                        const int normal[3] = {nx, ny, nz};
                        // Java BlockModelLighter.AdjacencyInfo corners[0] for this facing (used by its
                        // "both outer cells block light" fallback, which always reuses that edge).
                        int first[3] = {0, 0, 0};
                        if (ny < 0)
                            first[0] = -1;
                        else if (ny > 0)
                            first[0] = 1;
                        else if (nz < 0)
                            first[1] = 1;
                        else if (nz > 0)
                            first[0] = -1;
                        else if (nx < 0)
                            first[1] = 1;
                        else
                            first[1] = -1;
                        struct Packed {
                            int block, sky;
                        };
                        auto coordsAt = [&](const int p[3]) {
                            const std::array<int, 2> l = sample(p[0], p[1], p[2]);
                            return Packed{l[0] * 16, l[1] * 16};
                        };
                        auto smoothBlend = [](Packed n1, Packed n2, Packed n3, Packed c) {
                            if (c.sky > 2 * 16 - 1 || c.block > 2 * 16 - 1) {
                                for (Packed* n : {&n1, &n2, &n3}) {
                                    if (n->block == 0 && n->sky == 0)
                                        *n = c;
                                    else if (n->sky == 0)
                                        n->sky = c.sky;
                                }
                            }
                            return Packed{(n1.block + n2.block + n3.block + c.block) >> 2,
                                          (n1.sky + n2.sky + n3.sky + c.sky) >> 2};
                        };
                        const float shadeCenter = AoShade(cache, library, center[0], center[1], center[2]);
                        const Packed lightCenter = coordsAt(center);
                        for (int i = 0; i < 4; i++) {
                            if (isWater) {
                                aoV[i] = 1.0f;
                                lt[i][0] = cellLight.block;
                                lt[i][1] = cellLight.sky;
                            } else {
                                const int d1 = c[i][axis1] - origin[axis1] > 0 ? 1 : -1;
                                const int d2 = c[i][axis2] - origin[axis2] > 0 ? 1 : -1;
                                int e1[3] = {center[0], center[1], center[2]};
                                int e2[3] = {center[0], center[1], center[2]};
                                int corner[3] = {center[0], center[1], center[2]};
                                e1[axis1] += d1;
                                e2[axis2] += d2;
                                corner[axis1] += d1;
                                corner[axis2] += d2;
                                const bool permeable1 = !cache.Occ(e1[0] + normal[0], e1[1] + normal[1], e1[2] + normal[2]);
                                const bool permeable2 = !cache.Occ(e2[0] + normal[0], e2[1] + normal[1], e2[2] + normal[2]);
                                const int fb[3] = {center[0] + first[0], center[1] + first[1], center[2] + first[2]};
                                float sc;
                                Packed lc;
                                if (!permeable1 && !permeable2) {
                                    sc = AoShade(cache, library, fb[0], fb[1], fb[2]);
                                    lc = coordsAt(fb);
                                } else {
                                    sc = AoShade(cache, library, corner[0], corner[1], corner[2]);
                                    lc = coordsAt(corner);
                                }
                                const float s1 = AoShade(cache, library, e1[0], e1[1], e1[2]);
                                const float s2 = AoShade(cache, library, e2[0], e2[1], e2[2]);
                                aoV[i] = (s1 + s2 + sc + shadeCenter) * 0.25f;
                                const Packed blended = smoothBlend(coordsAt(e1), coordsAt(e2), lc, lightCenter);
                                lt[i][0] = static_cast<float>(blended.block) / 16.0f;
                                lt[i][1] = static_cast<float>(blended.sky) / 16.0f;
                            }
                            kv[i] = aoV[i] * shade;
                            col[i][0] = base[0] * kv[i];
                            col[i][1] = base[1] * kv[i];
                            col[i][2] = base[2] * kv[i];
                        }
                        // Java never flips the quad diagonal; its X faces list vertices in the opposite
                        // winding from ours, so those are the ones that need the other split.
                        const bool flip = nx != 0;
                        tess.AddQuad(v, col, uv, flip, lt);
                        if (ovTile && ny == 0) {
                            float ou0, ov0, ou1, ov1;
                            BlockLibrary::TileUV(ovTile, ou0, ov0, ou1, ov1);
                            const float in = 0.02f / 16.0f;
                            float uv2[4][2], col2[4][3];
                            for (int i = 0; i < 4; i++) {
                                float fu = in + fuA[i] * (1.0f - 2 * in);
                                float fv = in + fvA[i] * (1.0f - 2 * in);
                                uv2[i][0] = ou0 + fu * (ou1 - ou0);
                                uv2[i][1] = ov0 + fv * (ov1 - ov0);
                                col2[i][0] = ovR * kv[i];
                                col2[i][1] = ovG * kv[i];
                                col2[i][2] = ovB * kv[i];
                            }
                            tess.AddQuad(v, col2, uv2, flip, lt);
                        }
                    };

                    float yBot = by, yTop = by + 1.0f;
                    float h00 = -1, h10 = -1, h01 = -1, h11 = -1;
                    if (isWater) {
                        constexpr float kLift = 0.001f;
                        h00 = by + WaterCornerHeight(cache, library, y, x, z) - kLift;
                        h10 = by + WaterCornerHeight(cache, library, y, x + 1, z) - kLift;
                        h01 = by + WaterCornerHeight(cache, library, y, x, z + 1) - kLift;
                        h11 = by + WaterCornerHeight(cache, library, y, x + 1, z + 1) - kLift;
                        yTop = std::max(std::max(h00, h10), std::max(h01, h11));
                    } else if (gi.slab) {
                        if (gi.slabTop)
                            yBot = by + 0.5f;
                        else
                            yTop = by + 0.5f;
                    }
                    float c00 = isWater ? h00 : yTop;
                    float c10 = isWater ? h10 : yTop;
                    float c11 = isWater ? h11 : yTop;
                    float c01 = isWater ? h01 : yTop;
                    float top[4][3] = {
                        {bx, c00, bz}, {bx + 1, c10, bz}, {bx + 1, c11, bz + 1}, {bx, c01, bz + 1}};
                    float bottom[4][3] = {
                        {bx, yBot, bz + 1}, {bx + 1, yBot, bz + 1}, {bx + 1, yBot, bz}, {bx, yBot, bz}};
                    float px[4][3] = {
                        {bx + 1, c10, bz}, {bx + 1, yBot, bz}, {bx + 1, yBot, bz + 1}, {bx + 1, c11, bz + 1}};
                    float nx[4][3] = {{bx, c01, bz + 1}, {bx, yBot, bz + 1}, {bx, yBot, bz}, {bx, c00, bz}};
                    float pz[4][3] = {
                        {bx, c01, bz + 1}, {bx + 1, c11, bz + 1}, {bx + 1, yBot, bz + 1}, {bx, yBot, bz + 1}};
                    float nz[4][3] = {{bx + 1, c10, bz}, {bx, c00, bz}, {bx, yBot, bz}, {bx + 1, yBot, bz}};

                    int tTop = 0, tSide = 0, tBottom = 0;
                    bool tintT = false, tintS = false, tintB = false, known = false;
                    float tr = 1, tg = 1, tb = 1;
                    std::string useTopTex, useSideTex, useBotTex;
                    if (app) {
                        tTop = PickTile(app->varTop, app->top, bhash);
                        tSide = PickTile(app->varSide, app->side, bhash);
                        tBottom = PickTile(app->varBottom, app->bottom, bhash);
                        tintT = app->tintTop;
                        tintS = app->tintSide;
                        tintB = app->tintBottom;
                        tr = app->tintR;
                        tg = app->tintG;
                        tb = app->tintB;
                        known = (tTop | tSide | tBottom) != 0;
                        useTopTex = app->texTop;
                        useSideTex = app->texSide;
                        useBotTex = app->texBottom;
                    }
                    float trT = tr, tgT = tg, tbT = tb;
                    float trS = tr, tgS = tg, tbS = tb;
                    float trB = tr, tgB = tg, tbB = tb;
                    bool fT = tintT, fS = tintS, fB = tintB;
                    if (known) {
                        auto ttT = biomeTintFor(useTopTex, tTop, true);
                        auto ttS = biomeTintFor(useSideTex, tSide, false);
                        auto ttB = biomeTintFor(useBotTex, tBottom, false);
                        if (ttT[3]) {
                            trT = ttT[0];
                            tgT = ttT[1];
                            tbT = ttT[2];
                            fT = true;
                        }
                        if (ttS[3]) {
                            trS = ttS[0];
                            tgS = ttS[1];
                            tbS = ttS[2];
                            fS = true;
                        }
                        if (ttB[3]) {
                            trB = ttB[0];
                            tgB = ttB[1];
                            tbB = ttB[2];
                            fB = true;
                        }
                    }
                    const int cTop[4][3] = {
                        {x, y + 1, z}, {x + 1, y + 1, z}, {x + 1, y + 1, z + 1}, {x, y + 1, z + 1}};
                    const int cBottom[4][3] = {{x, y, z + 1}, {x + 1, y, z + 1}, {x + 1, y, z}, {x, y, z}};
                    const int cPx[4][3] = {
                        {x + 1, y + 1, z}, {x + 1, y, z}, {x + 1, y, z + 1}, {x + 1, y + 1, z + 1}};
                    const int cNx[4][3] = {{x, y + 1, z + 1}, {x, y, z + 1}, {x, y, z}, {x, y + 1, z}};
                    const int cPz[4][3] = {
                        {x, y + 1, z + 1}, {x + 1, y + 1, z + 1}, {x + 1, y, z + 1}, {x, y, z + 1}};
                    const int cNz[4][3] = {{x + 1, y + 1, z}, {x, y + 1, z}, {x, y, z}, {x + 1, y, z}};
                    if (app && app->overlayFrac > 0) {
                        static bool once = false;
                        if (!once) {
                            once = true;
                            printf("[blocks] grass top: haveTints=%d known=%d fT=%d "
                                   "tile=%d sat=%.2f biome=%s tint=(%.2f,%.2f,%.2f)\n",
                                   (int)haveTints, (int)known, (int)fT, tTop,
                                   library ? library->SaturationOf(tTop) : -1.0f, biomeName.c_str(), trT, tgT,
                                   tbT);
                        }
                    }
                    tess.SetLayer(translucent ? RenderLayer::Translucent : RenderLayer::Solid);
                    if (isWater) {
                        face(0, 1, 0, tTop, trT, tgT, tbT, true, known, kShadeTop, cTop, top);
                        face(1, 0, 0, tSide, trS, tgS, tbS, true, known, kShadeX, cPx, px);
                        face(-1, 0, 0, tSide, trS, tgS, tbS, true, known, kShadeX, cNx, nx);
                        face(0, 0, 1, tSide, trS, tgS, tbS, true, known, kShadeZ, cPz, pz);
                        face(0, 0, -1, tSide, trS, tgS, tbS, true, known, kShadeZ, cNz, nz);
                    } else {
                        face(0, 1, 0, tTop, trT, tgT, tbT, fT, known, kShadeTop, cTop, top);
                        face(0, -1, 0, tBottom, trB, tgB, tbB, fB, known, kShadeBottom, cBottom, bottom);
                        if (app && app->overlayFrac > 0 && app->overlayTile && !gi.slab && fT && tSide) {
                            ovTile = app->overlayTile;
                            ovR = trT;
                            ovG = tgT;
                            ovB = tbT;
                        }
                        face(1, 0, 0, tSide, trS, tgS, tbS, fS, known, kShadeX, cPx, px);
                        face(-1, 0, 0, tSide, trS, tgS, tbS, fS, known, kShadeX, cNx, nx);
                        face(0, 0, 1, tSide, trS, tgS, tbS, fS, known, kShadeZ, cPz, pz);
                        face(0, 0, -1, tSide, trS, tgS, tbS, fS, known, kShadeZ, cNz, nz);
                        ovTile = 0;
                    }
                }
        }
    }
}
