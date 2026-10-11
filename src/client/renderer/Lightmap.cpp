#include "client/renderer/Lightmap.hpp"

#include <algorithm>
#include <cmath>

namespace perlin {
    namespace {
        float Lerp(float t, float a, float b) {
            return a + (b - a) * t;
        }

        float NotGamma(float v) {
            const float inv = 1.0f - v;
            return 1.0f - inv * inv * inv * inv;
        }
    }

    float LightBrightness(int level, float ambient) {
        const float f = static_cast<float>(level) / 15.0f;
        const float g = f / (4.0f - 3.0f * f);
        return Lerp(ambient, g, 1.0f);
    }

    void BuildLightmap(const LightmapParams& params, uint8_t out[kLightmapBytes]) {
        const float skyFactor = params.skyDarken * 0.95f + 0.05f;
        const float blockFactor = params.blockFlicker + 1.5f;
        const float tint[3] = {Lerp(0.35f, params.skyDarken, 1.0f), Lerp(0.35f, params.skyDarken, 1.0f),
                               1.0f};

        for (int sky = 0; sky < 16; sky++)
            for (int block = 0; block < 16; block++) {
                const float n = LightBrightness(sky, params.ambient) * skyFactor;
                const float o = LightBrightness(block, params.ambient) * blockFactor;
                float rgb[3] = {o, o * ((o * 0.6f + 0.4f) * 0.6f + 0.4f), o * (o * o * 0.6f + 0.4f)};
                for (int i = 0; i < 3; i++) {
                    rgb[i] += tint[i] * n;
                    rgb[i] = Lerp(0.04f, rgb[i], 0.75f);
                    rgb[i] = std::clamp(rgb[i], 0.0f, 1.0f);
                }
                for (int i = 0; i < 3; i++) {
                    rgb[i] = Lerp(std::max(0.0f, params.gamma), rgb[i], NotGamma(rgb[i]));
                    rgb[i] = Lerp(0.04f, rgb[i], 0.75f);
                    rgb[i] = std::clamp(rgb[i], 0.0f, 1.0f);
                }
                uint8_t* pixel = out + (static_cast<size_t>(sky) * kLightmapSize + block) * 4;
                pixel[0] = static_cast<uint8_t>(rgb[0] * 255.0f + 0.5f);
                pixel[1] = static_cast<uint8_t>(rgb[1] * 255.0f + 0.5f);
                pixel[2] = static_cast<uint8_t>(rgb[2] * 255.0f + 0.5f);
                pixel[3] = 255;
            }
    }
}
