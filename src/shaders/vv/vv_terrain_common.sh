#include "vv_common.sh"
#include "vv_block_sampling.sh"

SAMPLER2D(s_shadow, 2);

vec3 faceNormal(vec3 rel)
{
    vec3 n = cross(dFdx(rel), dFdy(rel));
    float l = length(n);
    return l > 1.0e-8 ? n / l : vec3(0.0, 1.0, 0.0);
}

// The directional shading Perlin bakes into vertex colours (top 1.0, sides 0.8/0.6, bottom 0.5).
float vanillaShade(vec3 n)
{
    float up = max(n.y, 0.0);
    float down = max(-n.y, 0.0);
    float z = abs(n.z);
    float x = abs(n.x);
    return clamp(up * 1.0 + down * 0.5 + z * 0.8 + x * 0.6, 0.4, 1.0);
}

// Soft sun shadow: 12 taps on a per-pixel rotated spiral, about a tenth of a block wide whatever
// the shadow map resolution (higher quality settings just get sharper, cleaner edges).
float sunShadow(vec3 world, vec3 n, float ndl)
{
    float texelWorld = u_shadowInfo.y * u_shadowInfo.x;
    vec3 p = world + n * (texelWorld * 1.5 + 0.02);
    vec3 sc = mul(u_shadowMtx, vec4(p, 1.0)).xyz;
    vec2 edge = min(sc.xy, vec2(1.0, 1.0) - sc.xy);
    float inside = smoothstep(0.0, 0.06, min(edge.x, edge.y));
    float bias = (texelWorld * 1.8 * (1.0 - ndl) + 0.05) / u_shadowInfo.z;
    float radius = clamp(0.07 / max(texelWorld, 1.0e-4), 1.0, 3.5) * u_shadowInfo.x;
    float spin = hash12(floor(p.xz * 16.0) + floor(p.y * 16.0) * 7.31) * 6.2831853; // per world texel
    float lit = 0.0;
    for (int i = 0; i < 12; i++)
    {
        float fi = float(i) + 0.5;
        float r = sqrt(fi / 12.0);
        float a = fi * 2.3999632 + spin;
        vec2 uv = sc.xy + vec2(cos(a), sin(a)) * r * radius;
        float stored = texture2DLod(s_shadow, uv, 0.0).r;
        lit += (sc.z - bias <= stored) ? 1.0 : 0.0;
    }
    lit /= 12.0;
    return u_shadowInfo.w < 0.5 ? -1.0 : mix(1.0, lit, inside);
}

// Lights a block surface and returns display colour.
// texRgb: atlas colour, tint: vertex colour (biome tint x ambient occlusion x vanilla face shade),
// light: block/sky light 0..1, backlight: 1 for foliage (light passes through), emissive 0..1.
vec3 vvLit(vec3 texRgb, vec3 tint, vec3 n, vec3 rel, vec2 light, float backlight, float emissive)
{
    float shade = vanillaShade(n);
    vec3 tintAo = clamp(tint / max(shade, 0.01), 0.0, 1.5);
    vec3 albedo = vvPow(clamp(texRgb * tintAo, 0.0, 1.0), vec3_splat(2.2));
    vec3 viewDir = normalize(rel);
    float dist = length(rel);
    vec3 world = rel + u_camera.xyz;

    float skyl = light.y;
    vec3 L = lightDirection();
    float ndl = dot(n, L);
    float sky = smoothstep(0.30, 0.95, skyl);

    // Direct light: soft shadow map, cloud shadows, foliage transmission.
    float direct = 0.0;
    float scatter = 0.0;
    if (ndl > 0.0 || backlight > 0.0)
    {
        float s = sunShadow(world, n, max(ndl, 0.0));
        if (s < 0.0)
            s = sky;
        s *= sky * vvCloudShadow(world);
        float VoL = clamp(dot(viewDir, L), 0.0, 1.0);
        scatter = vvPow(VoL, 10.0) * backlight * S_FOLIAGE * (1.0 - vvRain());
        float wrap = mix(max(ndl, 0.0), clamp(abs(ndl) * 0.6 + 0.35, 0.0, 1.0), backlight * clamp(S_FOLIAGE, 0.0, 1.0));
        direct = (wrap + scatter * 1.5) * s;
    }
    direct *= smoothstep(0.0, 0.08, L.y);

    // Sky light: brightest on top faces, bluish in shadow (that is most of the Vibrant Visuals look).
    float skyView = 0.78 + 0.22 * n.y;
    vec3 ambient = vvSkyAmbient() * skyl * skyl * skyView;
    // Light bounced off the sunlit ground fills the undersides a little.
    ambient += vvMainLight() * 0.06 * skyl * clamp(-n.y * 0.5 + 0.5, 0.0, 1.0) * smoothstep(0.0, 0.2, L.y);

    // Block light (torches, lava...) and the held item.
    float bl = light.x;
    float held = u_heldLight.x / 15.0;
    if (held > 0.001)
    {
        float reach = 3.0 + held * 11.0;
        float att = clamp(1.0 - dist / reach, 0.0, 1.0);
        bl = max(bl, att * att * held * (0.35 + 0.65 * max(dot(n, -viewDir), 0.0)));
    }
    vec3 block = vec3(1.0, 0.70, 0.42) * (vvPow(bl, 3.0) * 2.2 + bl * bl * 0.25) * S_BLOCK;

    vec3 minLight = vec3(0.008, 0.010, 0.016) * (1.0 - skyl * skyl);
    vec3 total = vvMainLight() * direct + ambient + block + minLight;
    vec3 hdr = albedo * total + albedo * emissive * 3.0;

    // Nether / End: the vanilla lightmap, gently warmed, through the same tonemap.
    vec3 lm = vvPow(sampleLightmap(light * 240.0), vec3_splat(2.2));
    vec3 hdrOther = albedo * (lm * 2.4 + vec3(0.05, 0.035, 0.04) * S_AMBIENT) * mix(shade, 1.0, 0.4) + albedo * emissive * 2.5;
    return vvTonemap(u_skyParams.z > 0.5 ? hdr : hdrOther);
}

// Aerial haze towards the sky's horizon colour, then the vanilla distance/environment fog.
vec3 vvFog(vec3 color, vec2 fogDistance, vec3 viewDir)
{
    float envUnderwater = step(u_fogEnv.y, 96.0) * step(u_fogEnv.y, u_fogEnv.w * 0.6);
    if (u_skyParams.z > 0.5 && envUnderwater < 0.5)
    {
        vec3 hazeDir = normalize(vec3(viewDir.x, max(viewDir.y, 0.0) * 0.5 + 0.04, viewDir.z));
        vec3 hazeColor = vvSkyGradient(hazeDir) + vvSunGlow(hazeDir) * 0.5;
        float density = 0.0028 * S_FOG * (1.0 + vvRain() * 2.0);
        float haze = 1.0 - exp(-fogDistance.x * density);
        color = mix(color, hazeColor, haze * 0.62);
    }
    float f = fogAmount(fogDistance.x, fogDistance.y);
    return mix(color, u_fogColor.rgb, f);
}

float isTile(float layer, float tile)
{
    return abs(layer - tile) < 0.5 ? 1.0 : 0.0;
}
