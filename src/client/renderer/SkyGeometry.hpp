#pragma once
#include <cstdint>
#include <vector>

namespace perlin {
    struct SkyVertex {
        float x, y, z;
        uint32_t rgba;
        float u, v;
    };
    static_assert(sizeof(SkyVertex) == 24);

    struct CloudParams {
        float offsetX = 0, offsetZ = 0, height = 0;
        float uvX = 0, uvZ = 0;
        float color[3] = {1, 1, 1};
    };

    constexpr float kCloudLevel = 192.0f;
    constexpr int kCloudTexture = 256;

    std::vector<SkyVertex> BuildStars();
    std::vector<SkyVertex> BuildClouds(bool fancy = true);
    CloudParams ComputeClouds(float eyeX, float eyeY, float eyeZ, float cloudTicks, float celestialAngle);
    void SkyRotation(float celestialAngle, float out[16]);
    int MoonPhase(int64_t day);
}
