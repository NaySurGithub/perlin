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
// xyz: direction to the sun (the moon is opposite), w: daylight 0..1
uniform vec4 u_sunDir;
// x: seconds, y: rain brightness (1 = clear), z: star brightness
uniform vec4 u_time;
// x: held item block light (0..15), y..: unused
uniform vec4 u_heldLight;
// x: water tile, y: lava tile, z: flowing water tile (atlas layer indices, <0 when unknown)
uniform vec4 u_tiles;
// x: cloud height, y: moon phase, z: 1 in the overworld
uniform vec4 u_skyParams;
uniform vec4 u_dynParams;
uniform vec4 u_dynLights[8];
uniform vec4 u_packSettings[4];
uniform mat4 u_shadowMtx;
// x: 1/size, y: box width (blocks), z: box depth (blocks), w: shadows active
uniform vec4 u_shadowInfo;

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

float fogAmount(float spherical, float cylindrical)
{
    return max(linearFog(spherical, u_fogEnv.x, u_fogEnv.y), linearFog(cylindrical, u_fogEnv.z, u_fogEnv.w)) *
           u_fogColor.a;
}

vec3 sampleLightmap(vec2 lightBytes)
{
    vec2 uv = clamp(lightBytes / 256.0 + vec2_splat(0.5 / 16.0), vec2_splat(0.5 / 16.0), vec2_splat(15.5 / 16.0));
    return texture2DLod(s_lightmap, uv, 0.0).rgb;
}

float luma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Filmic curve (Narkowicz ACES fit) blended with the source so vanilla colours stay recognisable.
vec3 filmic(vec3 c)
{
    vec3 x = c * 1.22;
    vec3 mapped = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    return mix(c, clamp(mapped, 0.0, 1.0), 0.62);
}

vec3 saturateColor(vec3 c, float amount)
{
    return mix(vec3_splat(luma(c)), c, amount);
}

vec3 sunColor()
{
    // Warm at the horizon, near white overhead.
    float h = clamp(u_sunDir.y, 0.0, 1.0);
    return mix(vec3(1.0, 0.55, 0.28), vec3(1.0, 0.93, 0.80), pow(abs(max(h, 0.0)), 0.45));
}

// The renderer's shadow map follows the sun, or the moon while the sun is below the horizon.
vec3 lightDirection()
{
    return u_sunDir.y >= 0.0 ? u_sunDir.xyz : -u_sunDir.xyz;
}

vec3 moonColor()
{
    return vec3(0.42, 0.55, 0.95);
}

// Radiance of the main light (sun by day, moon at night) before shadowing.
vec3 mainLightColor()
{
    float day = clamp(u_sunDir.w, 0.0, 1.0);
    vec3 sun = sunColor() * 1.0;
    vec3 moon = moonColor() * 0.20;
    return mix(moon, sun, smoothstep(0.05, 0.6, day)) * u_time.y;
}

float fbm2(vec2 p)
{
    float a = 0.5;
    float s = 0.0;
    for (int i = 0; i < 3; i++)
    {
        s += valueNoise(p) * a;
        p = p * 2.03 + vec2(17.1, 9.2);
        a *= 0.5;
    }
    return s;
}

#define S_EXPOSURE u_packSettings[0].x
#define S_SATURATION u_packSettings[0].y
#define S_VIBRANCE u_packSettings[0].z
#define S_SUN u_packSettings[0].w
#define S_AMBIENT u_packSettings[1].x
#define S_BLOCK u_packSettings[1].y
#define S_FOLIAGE u_packSettings[1].z

float bslRain()
{
    return clamp((1.0 - u_time.y) * 2.0, 0.0, 1.0);
}

vec3 bslLightColor()
{
    float tb = clamp(u_sunDir.y, 0.0, 1.0);
    float dfade = 1.0 - pow(abs(clamp(1.0 - tb, 0.0, 1.0)), 1.5);
    vec3 morning = vec3(255.0, 160.0, 80.0) / 255.0 * 1.2;
    vec3 day = vec3(196.0, 220.0, 255.0) / 255.0 * 1.4;
    vec3 night = vec3(96.0, 192.0, 255.0) / 255.0 * 0.3;
    vec3 c = mix(night, mix(morning, day, dfade), clamp(u_sunDir.w, 0.0, 1.0));
    vec3 weather = vec3(176.0, 224.0, 255.0) / 255.0 * 1.2;
    c = mix(c, vec3_splat(dot(c, vec3(0.299, 0.587, 0.114))) * weather, bslRain());
    return c * c * S_SUN;
}

vec3 bslAmbientColor()
{
    float tb = clamp(u_sunDir.y, 0.0, 1.0);
    float dfade = 1.0 - pow(abs(clamp(1.0 - tb, 0.0, 1.0)), 1.5);
    vec3 morning = vec3(255.0, 204.0, 144.0) / 255.0 * 0.35;
    vec3 day = vec3(120.0, 172.0, 255.0) / 255.0 * 0.6;
    vec3 night = vec3(96.0, 192.0, 255.0) / 255.0 * 0.6 * 0.3;
    vec3 c = mix(night, mix(morning, day, dfade), clamp(u_sunDir.w, 0.0, 1.0));
    vec3 weather = vec3(176.0, 224.0, 255.0) / 255.0 * 1.2;
    c = mix(c, vec3_splat(dot(c, vec3(0.299, 0.587, 0.114))) * weather, bslRain());
    return c * c * S_AMBIENT;
}

vec3 bslBlockColor()
{
    vec3 c = vec3(255.0, 212.0, 160.0) / 255.0 * 0.85;
    return c * c * S_BLOCK;
}

vec3 bslTonemap(vec3 color)
{
    color *= exp2(2.0 + S_EXPOSURE);
    float s = 1.0;
    float a = 0.03 * s;
    float b = 0.01 * s;
    float c = 1.0 - (a + b);
    float d = 1.0 - (a + a);
    vec3 desat = vec3(color.r * c + color.g * a + color.b * b,
                      color.r * a + color.g * d + color.b * a,
                      color.r * b + color.g * a + color.b * c);
    desat = max(desat, vec3_splat(0.0));
    desat = desat / sqrt((desat * desat) + 1.0);
    float inv = 1.0 / (c * d * c + a * a * b * 2.0 - b * d * b - a * a * c * 2.0);
    vec3 resat = vec3(
        (desat.r * (d * c - a * a) + desat.g * (b * a - a * c) + desat.b * (a * a - b * d)),
        (desat.r * (a * b - a * c) + desat.g * (c * c - b * b) + desat.b * (a * b - a * c)),
        (desat.r * (a * a - d * b) + desat.g * (a * b - c * a) + desat.b * (c * d - a * a))) * inv;
    return clamp(resat, 0.0, 1.0);
}

vec3 bslColorGrade(vec3 color)
{
    float grayVibrance = (color.r + color.g + color.b) / 3.0;
    float graySaturation = dot(color, vec3(0.299, 0.587, 0.114));
    float mn = min(color.r, min(color.g, color.b));
    float mx = max(color.r, max(color.g, color.b));
    float sat = (1.0 - (mx - mn)) * (1.0 - mx) * grayVibrance * 5.0;
    vec3 lightness = vec3_splat((mn + mx) * 0.5);
    color = mix(color, mix(color, lightness, 1.0 - S_VIBRANCE), sat);
    color = mix(color, lightness, (1.0 - lightness) * (2.0 - S_VIBRANCE) / 2.0 * abs(S_VIBRANCE - 1.0));
    color = color * S_SATURATION - graySaturation * (S_SATURATION - 1.0);
    return clamp(color, 0.0, 1.0);
}

vec3 bslFinish(vec3 linearHdr)
{
    vec3 mapped = bslTonemap(linearHdr);
    mapped = pow(abs(max(mapped, vec3_splat(0.0))), vec3_splat(1.0 / 2.2));
    return bslColorGrade(mapped);
}
