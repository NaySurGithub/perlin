#include <bgfx_shader.sh>

SAMPLER2DARRAY(s_atlas, 0);
SAMPLER2D(s_lightmap, 1);

uniform mat4 u_mvp;
uniform vec4 u_camera;
uniform vec4 u_fogColor;
uniform vec4 u_fogEnv;
uniform vec4 u_fogSky;
uniform vec4 u_modulator;
uniform vec4 u_textureInfo;
uniform vec4 u_chunk;
uniform vec4 u_cloudOffset;
uniform vec4 u_cloudColor;
uniform vec4 u_dynParams;     // x: 0 off, 1 fast, 2 fancy; y: light count
uniform vec4 u_dynLights[8];  // xyz relative to the camera, w block light level

// Block light (0..15) the dynamic lights add at a camera-relative point: level - distance, like OptiFine.
float dynamicLevel(vec3 rel)
{
    float best = 0.0;
    for (int i = 0; i < 8; i++)
    {
        float on = step(float(i) + 0.5, u_dynParams.y);
        best = max(best, on * (u_dynLights[i].w - length(rel - u_dynLights[i].xyz)));
    }
    return clamp(best, 0.0, 15.0);
}

float linearFog(float distance, float fogStart, float fogEnd)
{
    if (distance <= fogStart)
        return 0.0;
    if (distance >= fogEnd)
        return 1.0;
    return (distance - fogStart) / max(fogEnd - fogStart, 0.00001);
}

vec3 applyFog(vec3 color, float spherical, float cylindrical, float envStart, float envEnd, float rdStart,
              float rdEnd, vec4 fogColor)
{
    float fogValue = max(linearFog(spherical, envStart, envEnd), linearFog(cylindrical, rdStart, rdEnd));
    return mix(color, fogColor.rgb, fogValue * fogColor.a);
}

vec3 sampleLightmap(vec2 lightBytes)
{
    vec2 uv = clamp(lightBytes / 256.0 + vec2_splat(0.5 / 16.0), vec2_splat(0.5 / 16.0), vec2_splat(15.5 / 16.0));
    return texture2DLod(s_lightmap, uv, 0.0).rgb;
}

