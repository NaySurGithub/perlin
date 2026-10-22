#pragma once
#include "client/javashaders/ProgramSet.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace perlin::jshader {
    constexpr int kMaxColorTex = 16;
    constexpr int kMaxShadowColor = 2;

    enum class BufferFormat { RGBA8, RGBA16, RGBA16F, RGBA32F, R8, RG8, R16F, RG16F, R32F, RGB8, RGB16F, R11F_G11F_B10F, RGB10_A2, RG16, R16, RGBA8_SNORM, RGB16, RGBA32I, RG32F };
    BufferFormat ParseBufferFormat(const std::string& text, BufferFormat fallback);

    struct BlendFactors {
        bool set = false;       // false = engine default
        bool off = false;       // blending disabled
        int src = 0, dst = 0, srcA = 0, dstA = 0;   // GL enum values
    };

    struct AlphaTest {
        bool set = false;
        bool off = false;
        int func = 0x204;       // GL_GREATER
        float ref = 0.1f;
    };

    struct BufferSetup {
        BufferFormat format = BufferFormat::RGBA8;
        bool clear = true;
        float clearColor[4] = {0, 0, 0, 0};
        bool mipmap = false;    // generate mips before each composite pass
        bool linear = true;
        float scaleX = 1.0f, scaleY = 1.0f;  // from size.buffer.* / scale
        int fixedWidth = 0, fixedHeight = 0;
    };

    struct CustomTexture {
        std::string stage;      // "" / "gbuffers" / "composite" / "deferred" / "shadowcomp" / "prepare" / "noise"
        std::string sampler;    // sampler name, e.g. "noisetex"
        std::string path;       // path inside the pack
        std::string type = "texture2D";
    };

    struct ProgramConfig {
        BlendFactors blend;
        std::map<int, BlendFactors> blendPerBuffer;
        AlphaTest alpha;
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        bool hasScale = false;
        int explicitEnabled = -1;                       // -1 not specified, 0 off, 1 on
        std::map<int, bool> flip;                       // colortex N -> flip request (composite chain ping-pong override)
        std::vector<int> drawBuffers;                   // filled by the pipeline from the program source
    };

    struct PipelineConfig {
        std::array<BufferSetup, kMaxColorTex> color;
        int shadowMapResolution = 2048;
        float shadowDistance = 128.0f;
        float shadowIntervalSize = 2.0f;
        float shadowDistanceRenderMul = -1.0f;
        bool shadowHardwareFiltering[2] = {false, false};
        bool shadowFilterLinear[2] = {true, true};      // false = nearest
        bool shadowMipmap[2] = {false, false};
        bool shadowColorLinear[2] = {true, true};
        BufferFormat shadowColorFormat[2] = {BufferFormat::RGBA8, BufferFormat::RGBA8};
        bool shadowColorClear[2] = {true, true};
        bool shadowEnabled = false;                     // any shadow program or shadowtex sampler present
        bool depthMipmap = false;
        float sunPathRotation = 0.0f;
        float eyeBrightnessHalflife = 10.0f, centerDepthHalflife = 1.0f, wetnessHalflife = 600.0f, drynessHalflife = 200.0f;
        float ambientOcclusionLevel = 1.0f;
        bool oldLighting = false, oldHandLight = true, separateAo = false, underwaterOverlay = true, sun = true, moon = true, vignette = true;
        bool rainDepth = false, beaconBeamDepth = false, dynamicHandLight = true, frustumCulling = true, shadowTranslucent = true;
        std::string clouds = "default";                 // default / off / fast / fancy
        std::string cloudShadow;                        // unused for now
        std::string particlesBeforeDeferred;
        std::map<std::string, ProgramConfig> programs;  // key: program name
        std::vector<CustomTexture> textures;
        std::string noiseTexture;                       // texture.noise
        std::vector<std::pair<std::string, std::string>> customUniformProps;  // uniform.* and variable.* entries
        std::vector<std::string> problems;

        const ProgramConfig* Find(const std::string& program) const {
            const auto it = programs.find(program);
            return it == programs.end() ? nullptr : &it->second;
        }
    };

    // Reads shaders.properties plus the `const` directives from the shader sources.
    PipelineConfig ParsePipelineConfig(ShaderPackInstance& pack);
    int GlEnumFromName(const std::string& name);
}
