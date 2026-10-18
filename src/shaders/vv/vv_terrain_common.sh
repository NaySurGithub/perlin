#include "vv_common.sh"
#include "vv_block_sampling.sh"

#define VV_TERRAIN_EXPOSURE 0.6

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

// Soft sun shadow: taps on a per-texel rotated spiral (1 / 6 / 12 / 20 by the filter setting),
// about a tenth of a block wide whatever the shadow map resolution (higher quality settings just
// get sharper, cleaner edges). Returns -1 when there is no shadow map.
float sunShadow(vec3 world, vec3 n, float ndl)
{
    float texelWorld = u_shadowInfo.y * u_shadowInfo.x;
    vec3 p = world + n * (texelWorld * 1.5 + 0.02);
    vec3 sc = mul(u_shadowMtx, vec4(p, 1.0)).xyz;
    vec2 edge = min(sc.xy, vec2(1.0, 1.0) - sc.xy);
    float inside = smoothstep(0.0, 0.06, min(edge.x, edge.y));
    float bias = (texelWorld * 1.8 * (1.0 - ndl) + 0.05) / u_shadowInfo.z;
    float lit = 1.0;
    VV_BRANCH if (inside > 0.0 && u_shadowInfo.w > 0.5)
    {
        // Hard / Soft / Softer / Softest = 1 / 6 / 12 / 20 taps on a per-texel rotated spiral.
        float filterLevel = floor(S_SHADOW_FILTER + 0.5);
        float taps = filterLevel < 0.5 ? 1.0 : (filterLevel < 1.5 ? 6.0 : (filterLevel < 2.5 ? 12.0 : 20.0));
        float radius = clamp(0.07 * S_SHADOW_SOFTNESS / max(texelWorld, 1.0e-4), 1.0, 5.0) * u_shadowInfo.x * step(1.5, taps);
        float spin = hash12(floor(p.xz * 16.0) + floor(p.y * 16.0) * 7.31) * 6.2831853; // per world texel
        float sum = 0.0;
        VV_LOOP for (int i = 0; i < 20; i++)
        {
            if (float(i) >= taps)
                break;
            float fi = float(i) + 0.5;
            float r = sqrt(fi / taps);
            float a = fi * 2.3999632 + spin;
            vec2 uv = sc.xy + vec2(cos(a), sin(a)) * r * radius;
            sum += (sc.z - bias <= texture2DLod(s_shadow, uv, 0.0).r) ? 1.0 : 0.0;
        }
        lit = mix(1.0, sum / taps, inside);
    }
    return u_shadowInfo.w < 0.5 ? -1.0 : lit;
}

// Lights a block surface and returns display colour.
// texRgb: atlas colour, tint: vertex colour (biome tint x ambient occlusion x vanilla face shade),
// light: block/sky light 0..1, backlight: 1 for foliage (light passes through), emissive 0..1.
// sunLight / skyLight: vvMainLight() / vvSkyAmbient(), worked out once per vertex.
vec3 vvLit(vec3 texRgb, vec3 tint, vec3 n, vec3 rel, vec2 light, float backlight, float emissive, vec3 sunLight, vec3 skyLight)
{
    float shade = vanillaShade(n);
    vec3 tintAo = clamp(tint / max(shade, 0.01), 0.0, 1.5);
    vec3 albedo = vvPow(clamp(texRgb * tintAo, 0.0, 1.0), vec3_splat(2.2));
    // Overworld and Nether/End are both worked out and one is picked at the end, with no branch:
    // an if/else here (together with the cloud shadow) makes FXC, the Direct3D 11 compiler,
    // crash on the translucent shader. The Nether path is a single lightmap read.
    vec3 hdr;
    {
        vec3 viewDir = normalize(rel);
        vec3 world = rel + u_camera.xyz;
        float skyl = light.y;
        vec3 L = lightDirection();
        float ndl = dot(n, L);
        float sky = smoothstep(0.30, 0.95, skyl);
        float sunUp = smoothstep(0.0, 0.08, L.y);

        // Direct light: soft shadow map, cloud shadows, foliage transmission. Skipped outright
        // underground, facing away, or with the light below the horizon.
        float direct = 0.0;
        VV_BRANCH if ((ndl > 0.0 || backlight > 0.0) && sky > 0.0 && sunUp > 0.0)
        {
            float s = sunShadow(world, n, max(ndl, 0.0));
            s = s < 0.0 ? 1.0 : mix(1.0, s, S_SHADOW_STRENGTH);
            s *= sky * vvCloudShadow(world);
            float VoL = clamp(dot(viewDir, L), 0.0, 1.0);
            float scatter = vvPow(VoL, 10.0) * backlight * S_FOLIAGE * (1.0 - vvRain());
            float wrap = mix(max(ndl, 0.0), clamp(abs(ndl) * 0.6 + 0.35, 0.0, 1.0), backlight * clamp(S_FOLIAGE, 0.0, 1.0));
            direct = (wrap + scatter * 1.5) * s * sunUp;
        }

        // Sky light: brightest on top faces, bluish in shadow (that is most of the Vibrant Visuals look).
        float skyView = 0.78 + 0.22 * n.y;
        vec3 ambient = skyLight * skyl * skyl * skyView;
        // Light bounced off the sunlit ground fills the undersides a little.
        ambient += sunLight * 0.06 * skyl * clamp(-n.y * 0.5 + 0.5, 0.0, 1.0) * smoothstep(0.0, 0.2, L.y);

        // Block light (torches, lava...) and the held item.
        float bl = light.x;
        float held = u_heldLight.x / 15.0;
        if (held > 0.001)
        {
            float reach = 3.0 + held * 11.0;
            float att = clamp(1.0 - length(rel) / reach, 0.0, 1.0);
            bl = max(bl, att * att * held * (0.35 + 0.65 * max(dot(n, -viewDir), 0.0)));
        }
        vec3 block = vec3(1.0, 0.70, 0.42) * (bl * bl * bl * 2.2 + bl * bl * 0.25) * S_BLOCK;

        vec3 minLight = vec3(0.008, 0.010, 0.016) * (1.0 - skyl * skyl);
        // VV_TERRAIN_EXPOSURE keeps sunlit blocks near their texture colours (at 1.0 noon terrain was
        // ~1.7x brighter than the texture and grass turned lime); torches and emissive blocks keep their full strength.
        hdr = albedo * ((sunLight * direct + ambient) * VV_TERRAIN_EXPOSURE + block + minLight) + albedo * emissive * 3.0;
    }
    // Nether / End: the vanilla lightmap, gently warmed, through the same tonemap.
    vec3 lm = vvPow(sampleLightmap(light * 240.0), vec3_splat(2.2));
    vec3 hdrOther = albedo * (lm * 2.4 + vec3(0.05, 0.035, 0.04) * S_AMBIENT) * mix(shade, 1.0, 0.4) + albedo * emissive * 2.5;
    return vvTonemap(u_skyParams.z > 0.5 ? hdr : hdrOther);
}

// Aerial haze (worked out per vertex: rgb colour, a amount), then the vanilla distance and
// environment fog.
vec3 vvFog(vec3 color, vec2 fogDistance, vec4 haze)
{
    color = mix(color, haze.rgb, haze.a);
    float f = fogAmount(fogDistance.x, fogDistance.y);
    return mix(color, u_fogColor.rgb, f);
}

float isTile(float layer, float tile)
{
    return abs(layer - tile) < 0.5 ? 1.0 : 0.0;
}
