#include "client/resources/BundledShaderPack.hpp"

#include <bgfx/embedded_shader.h>

#include "perlin_shaders.h"

namespace perlin {
    namespace {
        const bgfx::EmbeddedShader kBundled[] = {
            BGFX_EMBEDDED_SHADER(vv_vs_terrain),
            BGFX_EMBEDDED_SHADER(vv_fs_terrain_solid),
            BGFX_EMBEDDED_SHADER(vv_fs_terrain_cutout),
            BGFX_EMBEDDED_SHADER(vv_fs_terrain_translucent),
            BGFX_EMBEDDED_SHADER(vv_vs_sky),
            BGFX_EMBEDDED_SHADER(vv_fs_sky),
            BGFX_EMBEDDED_SHADER(vv_vs_star),
            BGFX_EMBEDDED_SHADER(vv_fs_star),
            BGFX_EMBEDDED_SHADER(vv_vs_sunrise),
            BGFX_EMBEDDED_SHADER(vv_fs_sunrise),
            BGFX_EMBEDDED_SHADER(vv_vs_celestial),
            BGFX_EMBEDDED_SHADER(vv_fs_celestial),
            BGFX_EMBEDDED_SHADER(vv_vs_cloud),
            BGFX_EMBEDDED_SHADER(vv_fs_cloud),
            BGFX_EMBEDDED_SHADER(vv_vs_shadow),
            BGFX_EMBEDDED_SHADER(vv_fs_shadow),
            BGFX_EMBEDDED_SHADER(vv_fs_shadow_cutout),
            BGFX_EMBEDDED_SHADER_END(),
        };


        const char* const kBundledMeta = R"({
  "name": "Vibrant Visuals",
  "description": "Bedrock's Vibrant Visuals look: atmospheric sky, square sun with bloom, blocky volumetric clouds with glowing edges, reflective water, soft shadows and haze",
  "shadows": true,
  "shadowMapSize": 2048,
  "shadowDistance": 96,
  "vanillaClouds": false,
  "sceneCopy": true,
  "settings": [
    {"id": "vvExposure", "label": "Exposure", "min": -2.0, "max": 2.0, "step": 0.25, "default": 0.0},
    {"id": "vvSaturation", "label": "Saturation", "min": 0.0, "max": 2.0, "step": 0.05, "default": 1.0},
    {"id": "vvContrast", "label": "Contrast", "min": 0.5, "max": 1.5, "step": 0.05, "default": 1.0},
    {"id": "vvSun", "label": "Sunlight Intensity", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "vvAmbient", "label": "Sky Light Intensity", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "vvBlock", "label": "Block Light Intensity", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "vvFoliage", "label": "Foliage Translucency", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0},
    {"id": "vvFog", "label": "Haze", "min": 0.0, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "vvReflections", "label": "Screen-Space Reflections", "type": "toggle", "default": 1},
    {"id": "vvClouds", "label": "Clouds", "type": "toggle", "default": 1},
    {"id": "vvCloudShadows", "label": "Cloud Shadows", "type": "toggle", "default": 1},
    {"id": "vvWaving", "label": "Waving Foliage", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0}
  ]
})";

        bgfx::RendererType::Enum RendererOf(const std::string& backend) {
            if (backend == "dxbc")
                return bgfx::RendererType::Direct3D11;
            if (backend == "glsl")
                return bgfx::RendererType::OpenGL;
            if (backend == "spirv")
                return bgfx::RendererType::Vulkan;
            if (backend == "metal")
                return bgfx::RendererType::Metal;
            if (backend == "essl")
                return bgfx::RendererType::OpenGLES;
            return bgfx::RendererType::Noop;
        }

        const bgfx::EmbeddedShader::Data* BundledData(const std::string& backend, const std::string& shader) {
            const bgfx::RendererType::Enum want = RendererOf(backend);
            const std::string name = "vv_" + shader;
            for (const bgfx::EmbeddedShader* it = kBundled; it->name != nullptr; ++it) {
                if (name != it->name)
                    continue;
                for (const auto& d : it->data)
                    if (d.type == want && d.data != nullptr && d.size > 0)
                        return &d;
            }
            return nullptr;
        }

    }

    const char* BundledShaderMeta() { return kBundledMeta; }

    bool BundledShaderBytes(const std::string& backend, const std::string& shader, const uint8_t*& data, size_t& size) {
        const bgfx::EmbeddedShader::Data* d = BundledData(backend, shader);
        if (!d)
            return false;
        data = d->data;
        size = d->size;
        return true;
    }
}
