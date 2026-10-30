#pragma once
// the density graph flattened for the compute kernel: node records, noise layers with their permutation tables, splines and the evaluation order
#include "world/vanilla/DensityFunction.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perlin::server::vanilla {

    struct GpuProgram {
        static constexpr uint32_t kNodeStride = 12;
        static constexpr uint32_t kLayerStride = 12;
        static constexpr uint32_t kBlendStride = 16;
        static constexpr uint32_t kMaxNodes = 1024;

        std::vector<uint32_t> nodes;    // kNodeStride words per node, in dependency order
        std::vector<uint32_t> aux;      // splines and interval-select tables
        std::vector<uint32_t> noises;   // per noise: first layer, layer count
        std::vector<uint32_t> layers;   // kLayerStride words per perlin layer
        std::vector<uint32_t> perms;    // 256 words per layer
        std::vector<uint32_t> blends;   // kBlendStride words per old_blended_noise
        std::vector<uint32_t> order;    // node indices: first `innerCount` run at the cell corners, the rest per block
        std::vector<uint32_t> interpolatedInputs;  // node whose value each interpolated node samples at the corners
        uint32_t nodeCount = 0, innerCount = 0, outerCount = 0, root = 0;

        // null with `error` set when the graph uses something the kernel cannot do
        static std::unique_ptr<GpuProgram> Build(const DfPtr& root, std::string& error);
    };
}
