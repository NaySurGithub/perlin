#pragma once
#include <cmath>
#include <cstdint>

namespace perlin {
    class RenderContext;
}

namespace perlin::ui {
    uint32_t CreateGlintTexture(RenderContext& renderer, int* width = nullptr);

    struct GlintTransform {
        float matrix[4]; // layer 0 U scroll, layer 1 U scroll, U scale, V scale
        float offset[2]; // unused
    };

    // Bedrock's foil clock: independent 1750 ms and 3000 ms periods, both scrolling along U.
    inline GlintTransform MakeGlintTransform(uint64_t millis, float scale = 0.5f) {
        const float a = -static_cast<float>(millis % 1750) / 1750.0f;
        const float b = static_cast<float>(millis % 3000) / 3000.0f;
        return {{a, b, scale, scale}, {0.0f, 0.0f}};
    }
}
