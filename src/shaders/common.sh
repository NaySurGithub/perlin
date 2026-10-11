#include <bgfx_shader.sh>

SAMPLER2D(s_atlas, 0);
SAMPLER2D(s_lightmap, 1);

uniform mat4 u_mvp;
uniform mat4 u_inverseViewProjection;
uniform vec4 u_camera;
uniform vec4 u_fog;
uniform vec4 u_fogColor;
uniform vec4 u_skyColor;
uniform vec4 u_sunDir;
uniform vec4 u_sunrise;
uniform vec4 u_time;

#define LIGHTMAP_SIZE 16.0

vec3 lightmapColor(vec2 blockSky)
{
    vec2 uv = (clamp(blockSky, 0.0, 1.0) * (LIGHTMAP_SIZE - 1.0) + 0.5) / LIGHTMAP_SIZE;
    return texture2D(s_lightmap, uv).rgb;
}

float cylindricalDistance(vec3 relative)
{
    return max(length(relative.xz), abs(relative.y));
}

float fogAmount(float distance)
{
    return smoothstep(u_fog.x, u_fog.y, distance);
}

vec3 applyFog(vec3 color, float distance)
{
    return mix(color, u_fogColor.rgb, fogAmount(distance));
}
