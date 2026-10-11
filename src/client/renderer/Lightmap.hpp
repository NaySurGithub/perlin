#pragma once
#include <cstdint>

namespace perlin {
    constexpr int kLightmapSize = 16;
    constexpr int kLightmapBytes = kLightmapSize * kLightmapSize * 4;

    struct LightmapParams {
        float skyDarken = 1.0f;
        float gamma = 0.5f;
        float ambient = 0.0f;
        float blockFlicker = 0.0f;
    };

    float LightBrightness(int level, float ambient);
    void BuildLightmap(const LightmapParams& params, uint8_t out[kLightmapBytes]);
}
