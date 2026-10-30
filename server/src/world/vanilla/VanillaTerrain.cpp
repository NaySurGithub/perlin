#include "world/vanilla/VanillaTerrain.hpp"

#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"

#include <vector>

namespace perlin::server::vanilla {

    std::unique_ptr<VanillaCpuBackend> VanillaCpuBackend::Create(const ServerConfig& config, std::string& error) {
        std::unique_ptr<VanillaCpuBackend> b(new VanillaCpuBackend());
        b->pack = DataPack::Embedded();
        b->graph = std::make_unique<DensityGraph>(*b->pack, static_cast<int64_t>(config.seed));
        b->finalDensity = b->graph->Load("minecraft:overworld/final_density");
        if (!b->finalDensity) {
            error = "the built-in overworld data has no overworld/final_density (rebuild: tools/gen_overworld_shape.py output is damaged)";
            return nullptr;
        }
        b->climate = std::make_unique<VanillaClimate>(*b->graph);
        if (!b->climate->Ok()) {
            error = "the built-in overworld data has no climate functions";
            return nullptr;
        }
        b->surface = std::make_unique<VanillaSurface>();
        return b;
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
            std::array<uint8_t, 256> biomes;
            BiomesOf(density.data(), positions[n].x, positions[n].z, biomes.data());
            surface->Fill(density.data(), biomes.data(), out[n]);
        }
    }
}

namespace perlin::server::vanilla {
    void VanillaCpuBackend::BiomesOf(const float* density, int chunkX, int chunkZ, uint8_t* biomes) const {
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                const int top = VanillaSurface::TopOf(density + static_cast<size_t>((x * 16 + z) * kHeight));
                const BiomeSite site = SiteOf(chunkX * 16 + x, chunkZ * 16 + z, top);
                int32_t target[6];
                climate->Sample(site.x, site.y, site.z, target);
                biomes[x * 16 + z] = static_cast<uint8_t>(FindBiome(target));
            }
    }
}
