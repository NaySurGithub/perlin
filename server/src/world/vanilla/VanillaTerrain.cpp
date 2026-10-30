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
        const BlockCatalog& c = BlockCatalog::Instance();
        b->stone = c.DefaultBlock("stone");
        b->deepslate = c.DefaultBlock("deepslate");
        b->bedrock = c.DefaultBlock("bedrock");
        b->water = c.DefaultBlock("water");
        b->grass = c.DefaultBlock("grass_block");
        b->dirt = c.DefaultBlock("dirt");
        b->sand = c.DefaultBlock("sand");
        b->gravel = c.DefaultBlock("gravel");
        return b;
    }

    void VanillaCpuBackend::Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) {
        for (size_t n = 0; n < positions.size() && n < out.size(); n++) {
            DfCacheScope cache;
            const int bx = positions[n].x * 16, bz = positions[n].z * 16;
            // block per (x, z, y from the floor up)
            std::vector<uint16_t> blocks(static_cast<size_t>(16 * 16 * kHeight), BlockRegistry::kAir);
            const auto at = [&](int x, int z, int y) -> uint16_t& { return blocks[static_cast<size_t>((x * 16 + z) * kHeight + y)]; };
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++) {
                    int top = -1;
                    for (int y = kHeight - 1; y >= 0; y--) {
                        const int wy = kMinY + y;
                        if (finalDensity->Eval(bx + x, wy, bz + z) > 0.0f) {
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
                            for (int d = 1; d <= 3 && top - d >= 0; d++)
                                if (at(x, z, top - d) == stone)
                                    at(x, z, top - d) = dirt;
                        } else {
                            const uint16_t skin = wy >= seaLevel - 4 ? sand : gravel;
                            for (int d = 0; d <= 3 && top - d >= 0; d++)
                                if (at(x, z, top - d) == stone)
                                    at(x, z, top - d) = skin;
                        }
                    }
                    at(x, z, 0) = bedrock;
                }
            ChunkColumn& column = out[n];
            std::array<uint16_t, kCells> cells;
            for (int sec = 0; sec < kSections; sec++) {
                for (int x = 0; x < 16; x++)
                    for (int z = 0; z < 16; z++)
                        for (int y = 0; y < 16; y++)
                            cells[static_cast<size_t>(CellIndex(x, y, z))] = at(x, z, sec * 16 + y);
                column.SetSection(sec, cells.data());
            }
        }
    }
}
