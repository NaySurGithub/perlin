#pragma once

namespace perlin {
    constexpr int kTicksPerDay = 24000;

    struct SkyProfile {
        float skyColor[3];
        float fogColor[3];
        float ambientLight;

        static const SkyProfile& Overworld();
    };

    struct SkyState {
        float skyColor[3];
        float fogColor[3];
        float sunDirection[3];
        float sunriseColor[4];
        float celestialAngle;
        float skyDarken;
        float starBrightness;
    };

    float CelestialAngle(float tickOfDay);
    SkyState ComputeSky(const SkyProfile& profile, float tickOfDay, float rain = 0.0f, float thunder = 0.0f);

    struct FogRange {
        float start;
        float end;
    };
    FogRange TerrainFog(float renderDistanceBlocks);
    void BlendFogTowardSky(SkyState& state, float renderDistanceChunks);
}
