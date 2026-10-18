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
            BGFX_EMBEDDED_SHADER(vv_fs_shadow_glass),
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
    {"group": "Lighting", "id": "vvSun", "slot": 3, "label": "Sunlight Intensity", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "vvAmbient", "slot": 4, "label": "Sky Light Intensity", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "vvBlock", "slot": 5, "label": "Block Light Intensity", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "vvNight", "slot": 27, "label": "Night Brightness", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"group": "Shadows", "id": "vvShadowFilter", "slot": 12, "label": "Shadow Filtering", "min": 0, "max": 3, "step": 1, "default": 2, "labels": ["Hard", "Soft", "Softer", "Softest"]},
    {"id": "vvShadowSoftness", "slot": 13, "label": "Shadow Softness", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "vvShadowStrength", "slot": 14, "label": "Shadow Darkness", "min": 0.0, "max": 1.0, "step": 0.05, "default": 1.0},
    {"id": "vvCloudShadows", "slot": 10, "label": "Cloud Shadows", "type": "toggle", "default": 1},
    {"id": "vvCloudShadowStrength", "slot": 31, "label": "Cloud Shadow Darkness", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0},
    {"group": "Sky & Clouds", "id": "vvClouds", "slot": 9, "label": "Clouds", "type": "toggle", "default": 1},
    {"id": "vvCloudQuality", "slot": 15, "label": "Cloud Quality", "min": 0, "max": 2, "step": 1, "default": 1, "labels": ["Low", "Medium", "High"]},
    {"id": "vvCloudOpacity", "slot": 16, "label": "Cloud Density", "min": 0.25, "max": 2.0, "step": 0.05, "default": 1.0},
    {"id": "vvCloudThickness", "slot": 17, "label": "Cloud Thickness", "min": 2, "max": 12, "step": 1, "default": 6},
    {"id": "vvCloudGlow", "slot": 18, "label": "Cloud Edge Glow", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0},
    {"id": "vvSkyBrightness", "slot": 30, "label": "Sky Brightness", "min": 0.5, "max": 2.0, "step": 0.05, "default": 1.0},
    {"id": "vvSunSize", "slot": 24, "label": "Sun & Moon Size", "min": 0.5, "max": 2.0, "step": 0.1, "default": 1.0},
    {"id": "vvSunGlow", "slot": 25, "label": "Sun Glow", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0},
    {"id": "vvStars", "slot": 26, "label": "Star Brightness", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0},
    {"id": "vvFog", "slot": 7, "label": "Haze", "min": 0.0, "max": 3.0, "step": 0.25, "default": 1.0},
    {"group": "Water", "id": "vvReflections", "slot": 8, "label": "Reflections", "min": 0, "max": 3, "step": 1, "default": 2, "labels": ["Sky Only", "Low", "Medium", "High"]},
    {"id": "vvReflectedClouds", "slot": 23, "label": "Reflected Clouds", "type": "toggle", "default": 1},
    {"id": "vvWaterClarity", "slot": 19, "label": "Water Clarity", "min": 0.25, "max": 3.0, "step": 0.25, "default": 1.0},
    {"id": "vvWaterWaves", "slot": 20, "label": "Wave Strength", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0},
    {"id": "vvRefraction", "slot": 22, "label": "Refraction", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0},
    {"id": "vvWaterSparkle", "slot": 21, "label": "Sun Sparkles", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0},
    {"group": "Foliage", "id": "vvWaving", "slot": 11, "label": "Waving Foliage", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0},
    {"id": "vvFoliage", "slot": 6, "label": "Foliage Translucency", "min": 0.0, "max": 2.0, "step": 0.1, "default": 1.0},
    {"group": "Color", "id": "vvExposure", "slot": 0, "label": "Exposure", "min": -2.0, "max": 2.0, "step": 0.25, "default": 0.0},
    {"id": "vvSaturation", "slot": 1, "label": "Saturation", "min": 0.0, "max": 2.0, "step": 0.05, "default": 1.0},
    {"id": "vvContrast", "slot": 2, "label": "Contrast", "min": 0.5, "max": 1.5, "step": 0.05, "default": 1.0},
    {"id": "vvTonemap", "slot": 28, "label": "Tonemapper", "min": 0, "max": 2, "step": 1, "default": 0, "labels": ["Cinematic", "Filmic", "Soft"]},
    {"id": "vvTemperature", "slot": 29, "label": "Color Temperature", "min": -1.0, "max": 1.0, "step": 0.1, "default": 0.0}
  ],
  "presets": [
    {"name": "Performance",
     "engine": {"shadowQuality": 1, "shadowDistance": 48, "shadowEntities": 0, "bloom": 0},
     "settings": {"vvReflections": 1, "vvReflectedClouds": 0, "vvShadowFilter": 1, "vvShadowSoftness": 1.0, "vvCloudQuality": 0, "vvCloudShadows": 0, "vvWaving": 0, "vvWaterSparkle": 0.6}},
    {"name": "Balanced",
     "engine": {"shadowQuality": 2, "shadowDistance": 0, "shadowEntities": 1, "bloom": 0.4},
     "settings": {"vvReflections": 2, "vvReflectedClouds": 1, "vvShadowFilter": 2, "vvShadowSoftness": 1.0, "vvCloudQuality": 1, "vvCloudShadows": 1, "vvWaving": 1, "vvWaterSparkle": 1}},
    {"name": "Visuals",
     "engine": {"shadowQuality": 4, "shadowDistance": 160, "shadowEntities": 1, "bloom": 0.6},
     "settings": {"vvReflections": 3, "vvReflectedClouds": 1, "vvShadowFilter": 3, "vvShadowSoftness": 1.2, "vvCloudQuality": 2, "vvCloudShadows": 1, "vvWaving": 1, "vvWaterSparkle": 1}}
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
                // A real bgfx shader starts with a "VSH"/"FSH"/"CSH" header; a 1-byte placeholder means
                // the build could not compile this shader for that backend (see PerlinDxbcFallback in
                // CMakeLists.txt), and the built-in shader is used instead.
                for (const auto& d : it->data)
                    if (d.type == want && d.data != nullptr && d.size >= 16 && d.data[1] == 'S' && d.data[2] == 'H')
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
