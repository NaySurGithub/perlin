#include "client/renderer/shader/ShaderManager.hpp"

#include <bgfx/embedded_shader.h>

#include <algorithm>
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
            BGFX_EMBEDDED_SHADER(vs_celestial),
            BGFX_EMBEDDED_SHADER(fs_celestial),
            BGFX_EMBEDDED_SHADER(vs_cloud),
            BGFX_EMBEDDED_SHADER(fs_cloud),
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
            {"vs_celestial", "fs_celestial"},
            {"vs_cloud", "fs_cloud"},
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

        enum Slot { Camera, Fog, FogColor, SkyColor, SunDirection, Sunrise, Time, Cloud, CloudUv, CloudColor };
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
        mvp = bgfx::createUniform("u_mvp", bgfx::UniformType::Mat4);
        inverseViewProjection = bgfx::createUniform("u_inverseViewProjection", bgfx::UniformType::Mat4);
        camera = bgfx::createUniform("u_camera", bgfx::UniformType::Vec4);
        fog = bgfx::createUniform("u_fog", bgfx::UniformType::Vec4);
        fogColor = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
        skyColor = bgfx::createUniform("u_skyColor", bgfx::UniformType::Vec4);
        sunDirection = bgfx::createUniform("u_sunDir", bgfx::UniformType::Vec4);
        sunrise = bgfx::createUniform("u_sunrise", bgfx::UniformType::Vec4);
        time = bgfx::createUniform("u_time", bgfx::UniformType::Vec4);
        skyObject = bgfx::createUniform("u_skyObject", bgfx::UniformType::Vec4);
        cloud = bgfx::createUniform("u_cloud", bgfx::UniformType::Vec4);
        cloudUv = bgfx::createUniform("u_cloudUv", bgfx::UniformType::Vec4);
        cloudColor = bgfx::createUniform("u_cloudColor", bgfx::UniformType::Vec4);
        skySampler = bgfx::createUniform("s_sky", bgfx::UniformType::Sampler);
        return true;
    }

    void ShaderManager::Shutdown() {
        for (auto& program : programs) {
            if (bgfx::isValid(program))
                bgfx::destroy(program);
            program = BGFX_INVALID_HANDLE;
        }
        for (bgfx::UniformHandle* handle :
             {&atlasSampler, &lightmapSampler, &mvp, &inverseViewProjection, &camera, &fog, &fogColor,
              &skyColor, &sunDirection, &sunrise, &time, &skyObject, &cloud, &cloudUv, &cloudColor, &skySampler}) {
            if (bgfx::isValid(*handle))
                bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }

    void ShaderManager::SetFrame(const FrameParams& params, const float matrix[16]) {
        std::memcpy(modelViewProjection, matrix, sizeof(modelViewProjection));
        std::memcpy(inverse, params.inverseViewProjection, sizeof(inverse));

        float* c = values[Camera];
        c[0] = params.cameraPosition[0];
        c[1] = params.cameraPosition[1];
        c[2] = params.cameraPosition[2];
        c[3] = params.renderDistance;

        values[Fog][0] = params.fogStart;
        values[Fog][1] = params.fogEnd;
        values[Fog][2] = 0.0f;
        values[Fog][3] = 0.0f;

        for (int i = 0; i < 3; i++) {
            values[FogColor][i] = params.fogColor[i];
            values[SkyColor][i] = params.skyColor[i];
            values[SunDirection][i] = params.sunDirection[i];
        }
        values[FogColor][3] = 1.0f;
        values[SkyColor][3] = 1.0f;
        values[SunDirection][3] = params.celestialAngle;
        std::memcpy(values[Sunrise], params.sunriseColor, sizeof(float) * 4);

        values[Time][0] = params.timeSeconds;
        values[Time][1] = params.skyDarken;
        values[Time][2] = params.starBrightness;
        values[Time][3] = static_cast<float>(params.frameIndex);

        values[Cloud][0] = params.cloud.offsetX;
        values[Cloud][1] = params.cloud.offsetZ;
        values[Cloud][2] = params.cloud.height;
        values[CloudUv][0] = params.cloud.uvX;
        values[CloudUv][1] = params.cloud.uvZ;
        std::memcpy(values[CloudColor], params.cloud.color, sizeof(float) * 3);
        const float chunks = params.renderDistance / 16.0f;
        values[CloudColor][3] = std::max(256.0f, (chunks + 2.0f) * 24.0f);
    }

    void ShaderManager::SetMatrix(const float matrix[16]) const {
        bgfx::setUniform(mvp, matrix);
    }

    void ShaderManager::SetSkyObject(float alpha) const {
        const float value[4] = {1.0f, 1.0f, 1.0f, alpha};
        bgfx::setUniform(skyObject, value);
    }

    void ShaderManager::BindSky(bgfx::TextureHandle texture, uint32_t flags) const {
        bgfx::setTexture(2, skySampler, texture, flags);
    }

    void ShaderManager::ApplyUniforms() const {
        bgfx::setUniform(mvp, modelViewProjection);
        bgfx::setUniform(inverseViewProjection, inverse);
        bgfx::setUniform(camera, values[Camera]);
        bgfx::setUniform(fog, values[Fog]);
        bgfx::setUniform(fogColor, values[FogColor]);
        bgfx::setUniform(skyColor, values[SkyColor]);
        bgfx::setUniform(sunDirection, values[SunDirection]);
        bgfx::setUniform(sunrise, values[Sunrise]);
        bgfx::setUniform(time, values[Time]);
        bgfx::setUniform(cloud, values[Cloud]);
        bgfx::setUniform(cloudUv, values[CloudUv]);
        bgfx::setUniform(cloudColor, values[CloudColor]);
    }

    void ShaderManager::Apply(bgfx::TextureHandle atlas, bgfx::TextureHandle lightmap, uint32_t atlasFlags,
                              uint32_t lightmapFlags) const {
        bgfx::setTexture(0, atlasSampler, atlas, atlasFlags);
        bgfx::setTexture(1, lightmapSampler, lightmap, lightmapFlags);
    }
}
