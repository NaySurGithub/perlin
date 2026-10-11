#include "client/renderer/Sky.hpp"

#include <algorithm>
#include <cmath>

namespace perlin {
    namespace {
        constexpr float kTwoPi = 6.28318530718f;
        constexpr float kPi = 3.14159265359f;

        float Saturate(float v) {
            return std::clamp(v, 0.0f, 1.0f);
        }
    }

    const SkyProfile& SkyProfile::Overworld() {
        static const SkyProfile profile = {
            {0.4706f, 0.6549f, 1.0f},
            {0.7529f, 0.8471f, 1.0f},
            0.0f,
        };
        return profile;
    }

    float CelestialAngle(float tickOfDay) {
        float t = std::fmod(tickOfDay, static_cast<float>(kTicksPerDay));
        if (t < 0.0f)
            t += kTicksPerDay;
        float d = t / kTicksPerDay - 0.25f;
        d -= std::floor(d);
        const float e = 0.5f - std::cos(d * kPi) * 0.5f;
        return (d * 2.0f + e) / 3.0f;
    }

    SkyState ComputeSky(const SkyProfile& profile, float tickOfDay, float rain, float thunder) {
        SkyState out{};
        const float angle = CelestialAngle(tickOfDay);
        const float cosine = std::cos(angle * kTwoPi);

        const float brightness = Saturate(cosine * 2.0f + 0.5f);
        for (int i = 0; i < 3; i++)
            out.skyColor[i] = profile.skyColor[i] * brightness;
        const float fogScale[3] = {brightness * 0.94f + 0.06f, brightness * 0.94f + 0.06f,
                                   brightness * 0.91f + 0.09f};
        for (int i = 0; i < 3; i++)
            out.fogColor[i] = profile.fogColor[i] * fogScale[i];

        auto weather = [](float color[3], float amount, float grayFactor) {
            if (amount <= 0.0f)
                return;
            const float gray = (color[0] * 0.3f + color[1] * 0.59f + color[2] * 0.11f) * grayFactor;
            const float keep = 1.0f - amount * 0.75f;
            for (int i = 0; i < 3; i++)
                color[i] = color[i] * keep + gray * (1.0f - keep);
        };
        weather(out.skyColor, rain, 0.6f);
        weather(out.skyColor, thunder, 0.2f);
        weather(out.fogColor, rain, 0.6f);
        weather(out.fogColor, thunder, 0.2f);

        float darken = Saturate(cosine * 2.0f + 0.2f);
        darken *= 1.0f - rain * 5.0f / 16.0f;
        darken *= 1.0f - thunder * 5.0f / 16.0f;
        out.skyDarken = darken * 0.8f + 0.2f;

        out.celestialAngle = angle;
        out.sunDirection[0] = -std::sin(angle * kTwoPi);
        out.sunDirection[1] = cosine;
        out.sunDirection[2] = 0.0f;

        if (cosine >= -0.4f && cosine <= 0.4f) {
            const float h = cosine / 0.4f * 0.5f + 0.5f;
            float alpha = 1.0f - (1.0f - std::sin(h * kPi)) * 0.99f;
            alpha *= alpha;
            out.sunriseColor[0] = h * 0.3f + 0.7f;
            out.sunriseColor[1] = h * h * 0.7f + 0.2f;
            out.sunriseColor[2] = 0.2f;
            out.sunriseColor[3] = alpha * (1.0f - rain);
        }

        const float stars = Saturate(1.0f - (cosine * 2.0f + 0.25f));
        out.starBrightness = stars * stars * 0.5f * (1.0f - rain);
        return out;
    }

    FogRange TerrainFog(float renderDistanceBlocks) {
        const float band = std::clamp(renderDistanceBlocks / 10.0f, 4.0f, 64.0f);
        return {renderDistanceBlocks - band, renderDistanceBlocks};
    }

    void BlendFogTowardSky(SkyState& state, float renderDistanceChunks) {
        float f = 0.25f + 0.75f * renderDistanceChunks / 32.0f;
        f = 1.0f - std::pow(std::clamp(f, 0.0f, 1.0f), 0.25f);
        for (int i = 0; i < 3; i++)
            state.fogColor[i] += (state.skyColor[i] - state.fogColor[i]) * f;
    }
}
