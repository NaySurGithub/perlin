#include "client/renderer/shader/ShaderManager.hpp"

#include "client/renderer/shader/EmbeddedShaders.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "perlin_shaders.h"
#include "client/resources/ShaderPack.hpp"

namespace perlin {
    namespace {
        const bgfx::EmbeddedShader kShaders[] = {
            BGFX_EMBEDDED_SHADER(vs_terrain),
            BGFX_EMBEDDED_SHADER(fs_terrain_solid),
            BGFX_EMBEDDED_SHADER(fs_terrain_cutout),
            BGFX_EMBEDDED_SHADER(fs_terrain_translucent),
            BGFX_EMBEDDED_SHADER(vs_sky),
            BGFX_EMBEDDED_SHADER(fs_sky),
            BGFX_EMBEDDED_SHADER(vs_star),
            BGFX_EMBEDDED_SHADER(fs_star),
            BGFX_EMBEDDED_SHADER(vs_sunrise),
            BGFX_EMBEDDED_SHADER(fs_sunrise),
            BGFX_EMBEDDED_SHADER(vs_celestial),
            BGFX_EMBEDDED_SHADER(fs_celestial),
            BGFX_EMBEDDED_SHADER(vs_cloud),
            BGFX_EMBEDDED_SHADER(fs_cloud),
            BGFX_EMBEDDED_SHADER(vs_screen),
            BGFX_EMBEDDED_SHADER(fs_screen),
            BGFX_EMBEDDED_SHADER(vs_ui),
            BGFX_EMBEDDED_SHADER(fs_ui),
            BGFX_EMBEDDED_SHADER(vs_post),
            BGFX_EMBEDDED_SHADER(fs_post),
            BGFX_EMBEDDED_SHADER(vs_entity_shadow),
            BGFX_EMBEDDED_SHADER(fs_entity_shadow),
            BGFX_EMBEDDED_SHADER(fs_bloom),
            BGFX_EMBEDDED_SHADER(fs_upscale),
            BGFX_EMBEDDED_SHADER(fs_taa),
            BGFX_EMBEDDED_SHADER(fs_framegen),
            BGFX_EMBEDDED_SHADER_END(),
        };

        struct ProgramSource {
            const char* vertex;
            const char* fragment;
        };

        constexpr ProgramSource kSources[] = {
            {"vs_terrain", "fs_terrain_solid"},
            {"vs_terrain", "fs_terrain_cutout"},
            {"vs_terrain", "fs_terrain_translucent"},
            {"vs_sky", "fs_sky"},
            {"vs_star", "fs_star"},
            {"vs_sunrise", "fs_sunrise"},
            {"vs_celestial", "fs_celestial"},
            {"vs_cloud", "fs_cloud"},
            {"vs_screen", "fs_screen"},
            {"vs_ui", "fs_ui"},
            {"vs_post", "fs_post"},
            {"vs_shadow", "fs_shadow"},
            {"vs_shadow", "fs_shadow_cutout"},
            {"vs_shadow", "fs_shadow_glass"},
            {"vs_celestial", "fs_celestial"},
            {"vs_entity_shadow", "fs_entity_shadow"},
            {"vs_post", "fs_bloom"},
            {"vs_post", "fs_upscale"},
            {"vs_post", "fs_taa"},
            {"vs_post", "fs_framegen"},
        };
        static_assert(std::size(kSources) == static_cast<size_t>(ShaderProgram::Count));
        constexpr size_t kPackOnlyFrom = static_cast<size_t>(ShaderProgram::Shadow);
        constexpr size_t kBuiltinOnlyFrom = static_cast<size_t>(ShaderProgram::SpriteUi);

        const char* RendererDirectory(bgfx::RendererType::Enum type) {
            switch (type) {
            case bgfx::RendererType::Direct3D11:
            case bgfx::RendererType::Direct3D12:
                return "dxbc";
            case bgfx::RendererType::Metal:
                return "metal";
            case bgfx::RendererType::OpenGL:
                return "glsl";
            case bgfx::RendererType::OpenGLES:
                return "essl";
            case bgfx::RendererType::Vulkan:
                return "spirv";
            default:
                return "";
            }
        }

    }

    bgfx::ShaderHandle ShaderManager::Load(const char* name, bool fromPack) const {
        const bgfx::RendererType::Enum type = bgfx::getRendererType();
        if (fromPack) {
            std::vector<char> bytes;
            if (ShaderPack::Instance().Read(RendererDirectory(type), name, bytes)) {
                bgfx::ShaderHandle handle = bgfx::createShader(bgfx::copy(bytes.data(), (uint32_t)bytes.size()));
                if (bgfx::isValid(handle)) {
                    bgfx::setName(handle, name);
                    return handle;
                }
                std::cout << "[shader] pack shader rejected by the driver: " << name << std::endl;
            }
            return BGFX_INVALID_HANDLE;
        }
        return bgfx::createEmbeddedShader(kShaders, type, name);
    }

    bool ShaderManager::BuildPrograms() {
        const bool packActive = ShaderPack::Instance().Active();
        for (size_t i = 0; i < programs.size(); i++) {
            const ProgramSource& source = kSources[i];
            programs[i] = BGFX_INVALID_HANDLE;
            if (i >= kBuiltinOnlyFrom) {
                programs[i] = bgfx::createProgram(Load(source.vertex, false), Load(source.fragment, false), true);
                if (!bgfx::isValid(programs[i]))
                    std::cout << "[shader] built-in program failed: " << source.vertex << " + " << source.fragment << std::endl;
                continue;
            }
            if (i >= kPackOnlyFrom && !packActive)
                continue;
            if (packActive) {
                bgfx::ShaderHandle vertex = Load(source.vertex, true);
                bgfx::ShaderHandle fragment = Load(source.fragment, true);
                if (bgfx::isValid(vertex) && bgfx::isValid(fragment)) {
                    programs[i] = bgfx::createProgram(vertex, fragment, true);
                    if (bgfx::isValid(programs[i])) {
                        std::cout << "[shader] pack: " << source.vertex << " + " << source.fragment << std::endl;
                        continue;
                    }
                } else {
                    if (bgfx::isValid(vertex))
                        bgfx::destroy(vertex);
                    if (bgfx::isValid(fragment))
                        bgfx::destroy(fragment);
                }
            }
            if (i >= kPackOnlyFrom)
                continue;
            bgfx::ShaderHandle vertex = Load(source.vertex, false);
            bgfx::ShaderHandle fragment = Load(source.fragment, false);
            programs[i] = bgfx::createProgram(vertex, fragment, true);
            if (!bgfx::isValid(programs[i])) {
                std::cout << "[shader] program failed: " << source.vertex << " + " << source.fragment
                          << std::endl;
                return false;
            }
        }
        return true;
    }

    bool ShaderManager::Reload() {
        for (auto& program : programs) {
            if (bgfx::isValid(program))
                bgfx::destroy(program);
            program = BGFX_INVALID_HANDLE;
        }
        return BuildPrograms();
    }

    bool ShaderManager::Initialize() {
        if (!BuildPrograms())
            return false;
        atlasSampler = bgfx::createUniform("s_atlas", bgfx::UniformType::Sampler);
        lightmapSampler = bgfx::createUniform("s_lightmap", bgfx::UniformType::Sampler);
        skySampler = bgfx::createUniform("s_sky", bgfx::UniformType::Sampler);
        mvp = bgfx::createUniform("u_mvp", bgfx::UniformType::Mat4);
        camera = bgfx::createUniform("u_camera", bgfx::UniformType::Vec4);
        fogColor = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
        fogEnvironment = bgfx::createUniform("u_fogEnv", bgfx::UniformType::Vec4);
        fogSky = bgfx::createUniform("u_fogSky", bgfx::UniformType::Vec4);
        modulator = bgfx::createUniform("u_modulator", bgfx::UniformType::Vec4);
        textureInfo = bgfx::createUniform("u_textureInfo", bgfx::UniformType::Vec4);
        chunk = bgfx::createUniform("u_chunk", bgfx::UniformType::Vec4);
        cloudOffset = bgfx::createUniform("u_cloudOffset", bgfx::UniformType::Vec4);
        cloudColor = bgfx::createUniform("u_cloudColor", bgfx::UniformType::Vec4);
        sunDirection = bgfx::createUniform("u_sunDir", bgfx::UniformType::Vec4);
        timeUniform = bgfx::createUniform("u_time", bgfx::UniformType::Vec4);
        heldLight = bgfx::createUniform("u_heldLight", bgfx::UniformType::Vec4);
        dynParams = bgfx::createUniform("u_dynParams", bgfx::UniformType::Vec4);
        dynLights = bgfx::createUniform("u_dynLights", bgfx::UniformType::Vec4, 8);
        packSettings = bgfx::createUniform("u_packSettings", bgfx::UniformType::Vec4, 8);
        tiles = bgfx::createUniform("u_tiles", bgfx::UniformType::Vec4);
        skyParams = bgfx::createUniform("u_skyParams", bgfx::UniformType::Vec4);
        shadowMatrix = bgfx::createUniform("u_shadowMtx", bgfx::UniformType::Mat4);
        shadowInfo = bgfx::createUniform("u_shadowInfo", bgfx::UniformType::Vec4);
        shadowSampler = bgfx::createUniform("s_shadow", bgfx::UniformType::Sampler);
        shadowTintSampler = bgfx::createUniform("s_shadowTint", bgfx::UniformType::Sampler);
        shadowMatrixB = bgfx::createUniform("u_shadowMtxB", bgfx::UniformType::Mat4);
        shadowInfoB = bgfx::createUniform("u_shadowInfoB", bgfx::UniformType::Vec4);
        shadowSamplerB = bgfx::createUniform("s_shadowB", bgfx::UniformType::Sampler);
        shadowTintSamplerB = bgfx::createUniform("s_shadowTintB", bgfx::UniformType::Sampler);
        shadowMatrixE = bgfx::createUniform("u_shadowMtxE", bgfx::UniformType::Mat4);
        shadowInfoE = bgfx::createUniform("u_shadowInfoE", bgfx::UniformType::Vec4);
        shadowSamplerE = bgfx::createUniform("s_shadowEnt", bgfx::UniformType::Sampler);
        cloudsSampler = bgfx::createUniform("s_clouds", bgfx::UniformType::Sampler);
        cloudMap = bgfx::createUniform("u_cloudMap", bgfx::UniformType::Vec4);
        sceneColorSampler = bgfx::createUniform("s_sceneColor", bgfx::UniformType::Sampler);
        sceneDepthSampler = bgfx::createUniform("s_sceneDepth", bgfx::UniformType::Sampler);
        sceneInfo = bgfx::createUniform("u_sceneInfo", bgfx::UniformType::Vec4);
        invMvp = bgfx::createUniform("u_invMvp", bgfx::UniformType::Mat4);
        tileFlagsSampler = bgfx::createUniform("s_tileFlags", bgfx::UniformType::Sampler);
        return true;
    }

    void ShaderManager::Shutdown() {
        for (auto& program : programs) {
            if (bgfx::isValid(program))
                bgfx::destroy(program);
            program = BGFX_INVALID_HANDLE;
        }
        for (bgfx::UniformHandle* handle : {&atlasSampler, &lightmapSampler, &skySampler, &mvp, &camera, &fogColor,
                                            &fogEnvironment, &fogSky, &modulator, &textureInfo, &chunk, &cloudOffset,
                                            &cloudColor, &sunDirection, &timeUniform, &heldLight, &dynParams, &dynLights, &packSettings, &tiles, &skyParams,
                                            &shadowMatrix, &shadowInfo, &shadowSampler, &shadowTintSampler, &shadowMatrixB, &shadowInfoB, &shadowSamplerB, &shadowTintSamplerB, &shadowMatrixE, &shadowInfoE, &shadowSamplerE, &cloudsSampler, &cloudMap,
                                            &sceneColorSampler, &sceneDepthSampler, &sceneInfo, &invMvp, &tileFlagsSampler}) {
            if (bgfx::isValid(*handle))
                bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }

    void ShaderManager::SetFrame(const FrameParams& params, int tilePixels) {
        std::copy_n(params.cameraPosition, 3, cameraValue);
        std::copy_n(params.fogColor, 4, fogColorValue);
        fogEnvironmentValue[0] = params.environmentalFogStart;
        fogEnvironmentValue[1] = params.environmentalFogEnd;
        fogEnvironmentValue[2] = params.renderDistanceFogStart;
        fogEnvironmentValue[3] = params.renderDistanceFogEnd;
        fogSkyValue[0] = params.skyFogEnd;
        fogSkyValue[1] = params.cloudFogEnd;
        textureInfoValue[0] = 1.0f / static_cast<float>(std::max(1, tilePixels));
        textureInfoValue[1] = params.rgss ? 1.0f : 0.0f;

        const float sunY = std::cos(params.sky.sunAngle);
        sunDirectionValue[0] = -std::sin(params.sky.sunAngle);
        sunDirectionValue[1] = sunY;
        sunDirectionValue[2] = 0.0f;
        sunDirectionValue[3] = std::clamp((sunY + 0.1f) / 0.35f, 0.0f, 1.0f);
        timeValue[0] = params.timeSeconds;
        timeValue[1] = params.sky.rainBrightness;
        timeValue[2] = params.sky.starBrightness;
        timeValue[3] = 0.0f;
        skyParamsValue[0] = params.cloud.height;
        skyParamsValue[1] = std::floor(params.sky.moonPhase);
        skyParamsValue[2] = params.sky.enabled ? 1.0f : 0.0f;
        skyParamsValue[3] = static_cast<float>(params.cameraMedium);
        heldLightValue[0] = params.heldLight;
        dynParamsValue[0] = static_cast<float>(params.dynMode);
        dynParamsValue[1] = static_cast<float>(params.dynMode ? std::clamp(params.dynCount, 0, 8) : 0);
        dynParamsValue[2] = params.waterSurfaceAbove;
        std::copy_n(&params.dynLights[0][0], 32, &dynLightsValue[0][0]);
        std::copy_n(&params.packSettings[0], 32, &packSettingsValue[0][0]);
        tilesValue[0] = params.waterTile;
        tilesValue[1] = params.lavaTile;
        tilesValue[2] = params.waterFlowTile;
    }

    void ShaderManager::ApplyFrame() const {
        bgfx::setUniform(camera, cameraValue);
        bgfx::setUniform(fogColor, fogColorValue);
        bgfx::setUniform(fogEnvironment, fogEnvironmentValue);
        bgfx::setUniform(fogSky, fogSkyValue);
        bgfx::setUniform(textureInfo, textureInfoValue);
        bgfx::setUniform(sunDirection, sunDirectionValue);
        bgfx::setUniform(timeUniform, timeValue);
        bgfx::setUniform(heldLight, heldLightValue);
        bgfx::setUniform(dynParams, dynParamsValue);
        bgfx::setUniform(dynLights, dynLightsValue, 8);
        bgfx::setUniform(packSettings, packSettingsValue, 8);
        bgfx::setUniform(tiles, tilesValue);
        bgfx::setUniform(skyParams, skyParamsValue);
        bgfx::setUniform(cloudMap, cloudMapValue);
        bgfx::setUniform(sceneInfo, sceneInfoValue);
        bgfx::setUniform(invMvp, invMvpValue);
        SetChunkVisibility(1.0f);
    }

    void ShaderManager::BindClouds(bgfx::TextureHandle texture) const {
        if (bgfx::isValid(texture))
            bgfx::setTexture(3, cloudsSampler, texture, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    }

    void ShaderManager::SetScene(const float info[4], const float inverseMvp[16]) {
        std::copy_n(info, 4, sceneInfoValue);
        std::copy_n(inverseMvp, 16, invMvpValue);
    }

    void ShaderManager::BindTileFlags(bgfx::TextureHandle texture) const {
        if (bgfx::isValid(texture))
            bgfx::setTexture(6, tileFlagsSampler, texture, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }

    void ShaderManager::BindScene(bgfx::TextureHandle color, bgfx::TextureHandle depth) const {
        constexpr uint32_t kFlags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        bgfx::setTexture(4, sceneColorSampler, color, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        bgfx::setTexture(5, sceneDepthSampler, depth, kFlags);
    }

    void ShaderManager::SetMatrix(const float matrix[16]) const {
        bgfx::setUniform(mvp, matrix);
    }

    void ShaderManager::SetModulator(float r, float g, float b, float a) const {
        const float value[4] = {r, g, b, a};
        bgfx::setUniform(modulator, value);
    }

    void ShaderManager::SetChunkVisibility(float visibility) const {
        const float value[4] = {visibility, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(chunk, value);
    }

    void ShaderManager::SetCloud(const float offset[3], const float color[4]) const {
        const float position[4] = {offset[0], offset[1], offset[2], 0.0f};
        bgfx::setUniform(cloudOffset, position);
        bgfx::setUniform(cloudColor, color);
    }

    void ShaderManager::BindSky(bgfx::TextureHandle texture, uint32_t flags) const {
        bgfx::setTexture(2, skySampler, texture, flags);
    }

    void ShaderManager::SetShadow(const float sample[16], const float info[4], bool enabled) const {
        bgfx::setUniform(shadowMatrix, sample);
        const float value[4] = {info[0], info[1], info[2], enabled ? 1.0f : 0.0f};
        bgfx::setUniform(shadowInfo, value);
    }

    void ShaderManager::BindShadow(bgfx::TextureHandle texture) const {
        bgfx::setTexture(2, shadowSampler, texture, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                                       BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }

    void ShaderManager::SetShadowNext(const float sample[16], const float info[4], float weight) const {
        bgfx::setUniform(shadowMatrixB, sample);
        const float value[4] = {info[0], info[1], info[2], weight};
        bgfx::setUniform(shadowInfoB, value);
    }

    void ShaderManager::BindShadowNext(bgfx::TextureHandle shadow, bgfx::TextureHandle tint) const {
        constexpr uint32_t kFlags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        bgfx::setTexture(8, shadowSamplerB, shadow, kFlags);
        bgfx::setTexture(9, shadowTintSamplerB, tint, kFlags);
    }

    void ShaderManager::SetShadowEntities(const float sample[16], const float info[4], bool enabled) const {
        bgfx::setUniform(shadowMatrixE, sample);
        const float value[4] = {info[0], info[1], info[2], enabled ? 1.0f : 0.0f};
        bgfx::setUniform(shadowInfoE, value);
    }

    void ShaderManager::BindShadowEntities(bgfx::TextureHandle texture) const {
        bgfx::setTexture(10, shadowSamplerE, texture, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                                          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }

    void ShaderManager::BindShadowTint(bgfx::TextureHandle texture) const {
        bgfx::setTexture(7, shadowTintSampler, texture, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                                           BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }

    void ShaderManager::Apply(bgfx::TextureHandle atlas, bgfx::TextureHandle lightmap, uint32_t atlasFlags,
                              uint32_t lightmapFlags) const {
        bgfx::setTexture(0, atlasSampler, atlas, atlasFlags);
        bgfx::setTexture(1, lightmapSampler, lightmap, lightmapFlags);
    }
}
