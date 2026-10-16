#include "client/resources/BundledShaderPack.hpp"

#include <bgfx/embedded_shader.h>

#include "perlin_shaders.h"

namespace perlin {
    namespace {
        const bgfx::EmbeddedShader kBundled[] = {
            BGFX_EMBEDDED_SHADER(bsl_vs_terrain),
            BGFX_EMBEDDED_SHADER(bsl_fs_terrain_solid),
            BGFX_EMBEDDED_SHADER(bsl_fs_terrain_cutout),
            BGFX_EMBEDDED_SHADER(bsl_fs_terrain_translucent),
            BGFX_EMBEDDED_SHADER(bsl_vs_sky),
            BGFX_EMBEDDED_SHADER(bsl_fs_sky),
            BGFX_EMBEDDED_SHADER(bsl_vs_star),
            BGFX_EMBEDDED_SHADER(bsl_fs_star),
            BGFX_EMBEDDED_SHADER(bsl_vs_sunrise),
            BGFX_EMBEDDED_SHADER(bsl_fs_sunrise),
            BGFX_EMBEDDED_SHADER(bsl_vs_celestial),
            BGFX_EMBEDDED_SHADER(bsl_fs_celestial),
            BGFX_EMBEDDED_SHADER(bsl_vs_cloud),
            BGFX_EMBEDDED_SHADER(bsl_fs_cloud),
            BGFX_EMBEDDED_SHADER(bsl_vs_shadow),
            BGFX_EMBEDDED_SHADER(bsl_fs_shadow),
            BGFX_EMBEDDED_SHADER(bsl_fs_shadow_cutout),
            BGFX_EMBEDDED_SHADER_END(),
        };


        const char* const kBundledMeta = R"({
  "name": "BSL",
  "description": "Soft shadows, warm sun and cool shade, filmic tonemapping, vibrance, and tunable lighting in the style of BSL",
  "shadows": true,
  "shadowMapSize": 2048,
  "shadowDistance": 90,
  "vanillaClouds": false,
  "settings": [
    {"id": "exposure", "label": "Exposure", "min": -2.0, "max": 2.0, "step": 0.25, "default": -0.75},
    {"id": "saturation", "label": "Saturation", "min": 0.0, "max": 2.0, "step": 0.05, "default": 1.0},
    {"id": "vibrance", "label": "Vibrance", "min": 0.0, "max": 2.0, "step": 0.05, "default": 1.0},
    {"id": "sunIntensity", "label": "Sunlight Intensity", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "ambientIntensity", "label": "Ambient Light Intensity", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "blockLightIntensity", "label": "Block Light Intensity", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "foliageGlow", "label": "Foliage Translucency", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0}
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
            const std::string name = "bsl_" + shader;
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
