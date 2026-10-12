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
