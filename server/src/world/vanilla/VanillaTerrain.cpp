#include "world/vanilla/VanillaTerrain.hpp"

#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"

#include <vector>

namespace perlin::server::vanilla {

    std::unique_ptr<VanillaCpuBackend> VanillaCpuBackend::Create(const ServerConfig& config, std::string& error) {
        if (config.javaData.empty()) {
            error = "the vanilla generator reads Java Edition's worldgen data: set java_data to the 26.3 client jar (or a folder holding data/minecraft)";
            return nullptr;
        }
        std::unique_ptr<VanillaCpuBackend> b(new VanillaCpuBackend());
        b->pack = DataPack::Open(config.javaData, error);
        if (!b->pack)
            return nullptr;
        b->graph = std::make_unique<DensityGraph>(*b->pack, static_cast<int64_t>(config.seed));
        b->finalDensity = b->graph->Load("minecraft:overworld/final_density");
        if (!b->finalDensity) {
            error = config.javaData + " has no worldgen/density_function/overworld/final_density.json";
            return nullptr;
        }
        b->blocks = std::make_unique<VanillaBlocks>();
        return b;
    }

    VanillaBlocks::VanillaBlocks() {
        const BlockCatalog& c = BlockCatalog::Instance();
        stone = c.DefaultBlock("stone");
        deepslate = c.DefaultBlock("deepslate");
        bedrock = c.DefaultBlock("bedrock");
        water = c.DefaultBlock("water");
        grass = c.DefaultBlock("grass_block");
        dirt = c.DefaultBlock("dirt");
        sand = c.DefaultBlock("sand");
        gravel = c.DefaultBlock("gravel");
    }

    void VanillaBlocks::Fill(const float* density, ChunkColumn& column) const {
        std::vector<uint16_t> blocks(static_cast<size_t>(16 * 16 * kHeight), BlockRegistry::kAir);
        const auto at = [&](int x, int z, int y) -> uint16_t& { return blocks[static_cast<size_t>((x * 16 + z) * kHeight + y)]; };
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                const float* d = density + static_cast<size_t>((x * 16 + z) * kHeight);
                int top = -1;
                for (int y = kHeight - 1; y >= 0; y--) {
                    const int wy = kMinY + y;
                    if (d[y] > 0.0f) {
                        at(x, z, y) = wy < 0 ? deepslate : stone;
                        if (top < 0)
                            top = y;
                    } else if (wy <= seaLevel) {
                        at(x, z, y) = water;
                    }
                }
                if (top >= 0) {
                    const int wy = kMinY + top;
                    // placeholder skin: dry land gets grass over dirt, the seabed sand near the shore and gravel deeper
                    if (wy >= seaLevel) {
                        at(x, z, top) = grass;
                        for (int dd = 1; dd <= 3 && top - dd >= 0; dd++)
                            if (at(x, z, top - dd) == stone)
                                at(x, z, top - dd) = dirt;
                    } else {
                        const uint16_t skin = wy >= seaLevel - 4 ? sand : gravel;
                        for (int dd = 0; dd <= 3 && top - dd >= 0; dd++)
                            if (at(x, z, top - dd) == stone)
                                at(x, z, top - dd) = skin;
                    }
                }
                at(x, z, 0) = bedrock;
            }
        std::array<uint16_t, kCells> cells;
        for (int sec = 0; sec < kSections; sec++) {
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < 16; y++)
                        cells[static_cast<size_t>(CellIndex(x, y, z))] = at(x, z, sec * 16 + y);
            column.SetSection(sec, cells.data());
        }
    }

    void VanillaCpuBackend::Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) {
        std::vector<float> density(static_cast<size_t>(16 * 16 * kHeight));
        for (size_t n = 0; n < positions.size() && n < out.size(); n++) {
            DfCacheScope cache;
            const int bx = positions[n].x * 16, bz = positions[n].z * 16;
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < kHeight; y++)
                        density[static_cast<size_t>((x * 16 + z) * kHeight + y)] = finalDensity->Eval(bx + x, kMinY + y, bz + z);
            blocks->Fill(density.data(), out[n]);
        }
    }
}
