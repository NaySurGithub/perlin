#pragma once
#include "client/renderer/RenderContext.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <string>

namespace perlin {
    enum class ShaderProgram : uint8_t {
        TerrainSolid,
        TerrainCutout,
        TerrainTranslucent,
        Sky,
        Star,
        Sunrise,
        Celestial,
        Cloud,
        Count
    };

    class ShaderManager {
    public:
        bool Initialize(const std::string& packDirectory);
        void Shutdown();

        bgfx::ProgramHandle Program(ShaderProgram program) const {
            return programs[static_cast<size_t>(program)];
        }

        void SetFrame(const FrameParams& params, int tilePixels);
        void ApplyFrame() const;
        void SetMatrix(const float matrix[16]) const;
        void SetModulator(float r, float g, float b, float a) const;
        void SetChunkVisibility(float visibility) const;
        void SetCloud(const float offset[3], const float color[4]) const;
        void BindSky(bgfx::TextureHandle texture, uint32_t flags) const;
        void Apply(bgfx::TextureHandle atlas, bgfx::TextureHandle lightmap, uint32_t atlasFlags,
                   uint32_t lightmapFlags) const;

    private:
        bgfx::ShaderHandle Load(const char* name, const std::string& packDirectory) const;

        std::array<bgfx::ProgramHandle, static_cast<size_t>(ShaderProgram::Count)> programs{};

        bgfx::UniformHandle atlasSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle lightmapSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle skySampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle mvp = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle camera = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle fogColor = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle fogEnvironment = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle fogSky = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle modulator = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle textureInfo = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle chunk = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle cloudOffset = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle cloudColor = BGFX_INVALID_HANDLE;

        float cameraValue[4] = {};
        float fogColorValue[4] = {};
        float fogEnvironmentValue[4] = {};
        float fogSkyValue[4] = {};
        float textureInfoValue[4] = {};
    };
}
