#pragma once
#include "world/Generator.hpp"
#include "world/vanilla/DensityFunction.hpp"

namespace perlin::server::vanilla {

    // what both backends share: block ids from a column of densities, then the placeholder surface
    class VanillaBlocks {
    public:
        VanillaBlocks();
        // density[(x * 16 + z) * kHeight + y], y counted from the world floor
        void Fill(const float* density, ChunkColumn& column) const;

    private:
        uint16_t stone, deepslate, bedrock, water, grass, dirt, sand, gravel;
        int seaLevel = 63;
    };

    // java terrain on the cpu: the overworld's final_density decides stone against air, sea level fills the rest with water.
    // surfaces are a placeholder until the material rules are ported (the ground is stone with a grass/dirt or sand/gravel skin)
    class VanillaCpuBackend final : public TerrainBackend {
    public:
        // null with `error` set when the data cannot be read
        static std::unique_ptr<VanillaCpuBackend> Create(const ServerConfig& config, std::string& error);

        const char* Name() const override { return "vanilla/cpu"; }
        void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) override;
        size_t PreferredBatch() const override { return 1; }
        bool ThreadSafe() const override { return true; }

        // block density > 0 means solid; exposed for tests
        float Density(int x, int y, int z) const { return finalDensity->Eval(x, y, z); }

    private:
        VanillaCpuBackend() = default;
        std::unique_ptr<DataPack> pack;
        std::unique_ptr<DensityGraph> graph;
        DfPtr finalDensity;
        std::unique_ptr<VanillaBlocks> blocks;
    };
}
