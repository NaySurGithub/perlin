#pragma once
#include "client/renderer/RenderContext.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <string>

namespace perlin {
    enum class ShaderProgram : uint8_t { TerrainSolid, TerrainCutout, TerrainTranslucent, Sky, Count };

    class ShaderManager {
    public:
        bool Initialize(const std::string& packDirectory);
        void Shutdown();

        bgfx::ProgramHandle Program(ShaderProgram program) const {
            return programs[static_cast<size_t>(program)];
        }

        void SetFrame(const FrameParams& params, const float modelViewProjection[16]);
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

        float values[7][4] = {};
        float modelViewProjection[16] = {};
        float inverse[16] = {};
    };
}
