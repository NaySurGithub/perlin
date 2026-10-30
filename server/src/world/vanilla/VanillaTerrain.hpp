#pragma once
#include "world/Generator.hpp"
#include "world/vanilla/DensityFunction.hpp"

namespace perlin::server::vanilla {

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
        uint16_t stone = 0, deepslate = 0, bedrock = 0, water = 0, grass = 0, dirt = 0, sand = 0, gravel = 0;
        int seaLevel = 63;
    };
}
