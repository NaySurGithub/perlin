#pragma once
#include "client/renderer/RenderContext.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
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
        Screen,
        Ui,
        Post,
        Shadow,
        ShadowCutout,
        // Always the built-in shaders, whatever pack is active:
        SpriteUi,     // HUD sprites such as the crosshair
        EntityShadow, // entity meshes into the pack's shadow map
        Bloom,
        Count
    };

    class ShaderManager {
    public:
        bool Initialize();
        bool Reload();
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
        bool Has(ShaderProgram program) const { return bgfx::isValid(programs[static_cast<size_t>(program)]); }
        void SetShadow(const float sample[16], const float info[4], bool enabled) const;
        void BindShadow(bgfx::TextureHandle texture) const;
        void Apply(bgfx::TextureHandle atlas, bgfx::TextureHandle lightmap, uint32_t atlasFlags,
                   uint32_t lightmapFlags) const;
        // Pack extras. Clouds: the vanilla cloud map (stage 3) and how world xz maps onto it.
        // Scene: copies of the opaque scene colour/depth for the translucent pass (stages 4, 5),
        // u_sceneInfo = (valid, depth is -1..1, origin bottom-left, unused), u_invMvp = inverse
        // of the world view-projection.
        void SetCloudMap(const float value[4]) { std::copy_n(value, 4, cloudMapValue); }
        void BindClouds(bgfx::TextureHandle texture) const;
        void SetScene(const float info[4], const float inverseMvp[16]);
        void BindScene(bgfx::TextureHandle color, bgfx::TextureHandle depth) const;
        // Per-atlas-tile flags for packs (stage 6): r = foliage kind (see the waving code).
        void BindTileFlags(bgfx::TextureHandle texture) const;

    private:
        bool BuildPrograms();
        bgfx::ShaderHandle Load(const char* name, bool fromPack) const;

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
        bgfx::UniformHandle sunDirection = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle timeUniform = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle heldLight = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle dynParams = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle dynLights = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle packSettings = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle tiles = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle skyParams = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle shadowMatrix = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle shadowInfo = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle shadowSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle cloudsSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle cloudMap = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle sceneColorSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle sceneDepthSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle sceneInfo = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle invMvp = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle tileFlagsSampler = BGFX_INVALID_HANDLE;
        float cloudMapValue[4] = {0.0f, 0.0f, 1.0f / 3072.0f, 1.0f / 3072.0f};
        float sceneInfoValue[4] = {};
        float invMvpValue[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

        float cameraValue[4] = {};
        float fogColorValue[4] = {};
        float fogEnvironmentValue[4] = {};
        float fogSkyValue[4] = {};
        float textureInfoValue[4] = {};
        float sunDirectionValue[4] = {};
        float timeValue[4] = {};
        float heldLightValue[4] = {};
        float dynParamsValue[4] = {};
        float dynLightsValue[8][4] = {};
        float packSettingsValue[4][4] = {};
        float tilesValue[4] = {};
        float skyParamsValue[4] = {};
    };
}
