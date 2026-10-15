#pragma once
#include <cmath>
#include <cstdint>

namespace perlin {
    class RenderContext;
}

namespace perlin::ui {
    uint32_t CreateGlintTexture(RenderContext& renderer, int* width = nullptr);

    struct GlintTransform {
        float matrix[4];
        float offset[2];
    };

    inline GlintTransform MakeGlintTransform(uint64_t millis, float speed = 0.5f, float spriteScale = 0.25f) {
        const uint64_t l = static_cast<uint64_t>(static_cast<double>(millis) * speed * 8.0);
        const float f = static_cast<float>(l % 110000) / 110000.0f;
        const float g = static_cast<float>(l % 30000) / 30000.0f;
        const float a = 10.0f * 3.14159265f / 180.0f;
        const float c = std::cos(a) * spriteScale, s = std::sin(a) * spriteScale;
        return {{c, -s, s, c}, {-f, g}};
    }
}
