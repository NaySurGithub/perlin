#pragma once
#include "client/renderer/RenderContext.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <string>

namespace perlin {
    enum class ShaderProgram : uint8_t { TerrainSolid, TerrainCutout, TerrainTranslucent, Sky, Celestial, Cloud, Count };

    class ShaderManager {
    public:
        bool Initialize(const std::string& packDirectory);
        void Shutdown();

        bgfx::ProgramHandle Program(ShaderProgram program) const {
            return programs[static_cast<size_t>(program)];
        }

        void SetFrame(const FrameParams& params, const float modelViewProjection[16]);
        void ApplyUniforms() const;
        void SetMatrix(const float matrix[16]) const;
        void SetSkyObject(float alpha) const;
        void BindSky(bgfx::TextureHandle texture, uint32_t flags) const;
        void Apply(bgfx::TextureHandle atlas, bgfx::TextureHandle lightmap, uint32_t atlasFlags,
                   uint32_t lightmapFlags) const;

    private:
        bgfx::ShaderHandle Load(const char* name, const std::string& packDirectory) const;

        std::array<bgfx::ProgramHandle, static_cast<size_t>(ShaderProgram::Count)> programs{};

        bgfx::UniformHandle atlasSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle lightmapSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle mvp = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle inverseViewProjection = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle camera = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle fog = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle fogColor = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle skyColor = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle sunDirection = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle sunrise = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle time = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle skyObject = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle cloud = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle cloudUv = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle cloudColor = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle skySampler = BGFX_INVALID_HANDLE;

        float values[10][4] = {};
        float modelViewProjection[16] = {};
        float inverse[16] = {};
    };
}
