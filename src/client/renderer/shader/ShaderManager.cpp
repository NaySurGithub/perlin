#include "client/renderer/shader/ShaderManager.hpp"

#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "perlin_shaders.h"

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
        };

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

    bgfx::ShaderHandle ShaderManager::Load(const char* name, const std::string& packDirectory) const {
        const bgfx::RendererType::Enum type = bgfx::getRendererType();
        if (!packDirectory.empty()) {
            const std::string path = packDirectory + "/" + RendererDirectory(type) + "/" + name + ".bin";
            std::ifstream file(path, std::ios::binary);
            if (file) {
                std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
                if (!bytes.empty()) {
                    bgfx::ShaderHandle handle = bgfx::createShader(bgfx::copy(bytes.data(), bytes.size()));
                    if (bgfx::isValid(handle)) {
                        bgfx::setName(handle, name);
                        std::cout << "[shader] " << path << std::endl;
                        return handle;
                    }
                }
            }
        }
        return bgfx::createEmbeddedShader(kShaders, type, name);
    }

    bool ShaderManager::Initialize(const std::string& packDirectory) {
        for (size_t i = 0; i < programs.size(); i++) {
            const ProgramSource& source = kSources[i];
            bgfx::ShaderHandle vertex = Load(source.vertex, packDirectory);
            bgfx::ShaderHandle fragment = Load(source.fragment, packDirectory);
            programs[i] = bgfx::createProgram(vertex, fragment, true);
            if (!bgfx::isValid(programs[i])) {
                std::cout << "[shader] program failed: " << source.vertex << " + " << source.fragment
                          << std::endl;
                return false;
            }
        }

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
                                            &cloudColor, &sunDirection, &timeUniform}) {
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

        // Sun pose is Ry(-90) * Rx(angle) * (0, 1, 0); the moon sits opposite.
        const float sunY = std::cos(params.sky.sunAngle);
        sunDirectionValue[0] = -std::sin(params.sky.sunAngle);
        sunDirectionValue[1] = sunY;
        sunDirectionValue[2] = 0.0f;
        sunDirectionValue[3] = std::clamp((sunY + 0.1f) / 0.35f, 0.0f, 1.0f);
        timeValue[0] = params.timeSeconds;
        timeValue[1] = params.sky.rainBrightness;
        timeValue[2] = params.sky.starBrightness;
        timeValue[3] = 0.0f;
    }

    void ShaderManager::ApplyFrame() const {
        bgfx::setUniform(camera, cameraValue);
        bgfx::setUniform(fogColor, fogColorValue);
        bgfx::setUniform(fogEnvironment, fogEnvironmentValue);
        bgfx::setUniform(fogSky, fogSkyValue);
        bgfx::setUniform(textureInfo, textureInfoValue);
        bgfx::setUniform(sunDirection, sunDirectionValue);
        bgfx::setUniform(timeUniform, timeValue);
        SetChunkVisibility(1.0f);
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

    void ShaderManager::Apply(bgfx::TextureHandle atlas, bgfx::TextureHandle lightmap, uint32_t atlasFlags,
                              uint32_t lightmapFlags) const {
        bgfx::setTexture(0, atlasSampler, atlas, atlasFlags);
        bgfx::setTexture(1, lightmapSampler, lightmap, lightmapFlags);
    }
}
