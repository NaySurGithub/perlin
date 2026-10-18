$input v_color0, v_texcoord0, v_fog, v_layer, v_visibility, v_rel, v_light, v_tint, v_foliage, v_sunLight, v_skyLight, v_haze

#include "vv_terrain_common.sh"

// Opaque scene behind the translucent pass (copied by the renderer when the pack sets sceneCopy).
SAMPLER2D(s_sceneColor, 4);
SAMPLER2D(s_sceneDepth, 5);

// Water extinction per block (red goes first: turquoise shallows, deep blue depths), divided by
// the Water Clarity setting.
#define WATER_EXTINCTION (vec3(0.78, 0.13, 0.19) / max(S_WATER_CLARITY, 0.1))

vec2 ndcToUv(vec2 ndc)
{
    return vec2(ndc.x * 0.5 + 0.5, u_sceneInfo.z > 0.5 ? ndc.y * 0.5 + 0.5 : 0.5 - ndc.y * 0.5);
}

vec2 uvToNdc(vec2 uv)
{
    return vec2(uv.x * 2.0 - 1.0, u_sceneInfo.z > 0.5 ? uv.y * 2.0 - 1.0 : 1.0 - uv.y * 2.0);
}

float clipDepthToTexture(float z)
{
    return u_sceneInfo.y > 0.5 ? z * 0.5 + 0.5 : z;
}

// World position of the opaque scene at a screen uv.
vec3 sceneWorld(vec2 uv, float depth)
{
    float z = u_sceneInfo.y > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec4 p = mul(u_invMvp, vec4(uvToNdc(uv), z, 1.0));
    return p.xyz / p.w;
}

float sceneDepthAt(vec2 uv)
{
    return texture2DLod(s_sceneDepth, uv, 0.0).r;
}

vec3 sceneColorAt(vec2 uv)
{
    return texture2DLod(s_sceneColor, uv, 0.0).rgb;
}

float edgeFade(vec2 uv)
{
    vec2 e = min(uv, vec2_splat(1.0) - uv);
    return smoothstep(0.0, 0.07, min(e.x, e.y));
}

// Screen-space reflection: march the reflected ray through the opaque depth copy.
// Returns rgb and a confidence in a (0 = missed).
// Quality 1 / 2 / 3 = 12 / 20 / 30 steps, with longer, faster growing steps at low quality so
// the reach stays about the same.
vec4 traceReflection(vec3 origin, vec3 dir, float jitter, float quality)
{
    vec3 cam = u_camera.xyz;
    float maxSteps = quality < 1.5 ? 12.0 : (quality < 2.5 ? 20.0 : 30.0);
    float stepLen = quality < 1.5 ? 0.6 : (quality < 2.5 ? 0.45 : 0.35);
    float growth = quality < 1.5 ? 1.32 : (quality < 2.5 ? 1.25 : 1.2);
    float t = 0.15 + jitter * stepLen;
    float prevT = 0.0;
    float hitT = -1.0;
    VV_LOOP for (int i = 0; i < 30; i++)
    {
        if (float(i) >= maxSteps)
            break;
        vec3 q = origin + dir * t;
        vec4 clip = mul(u_mvp, vec4(q, 1.0));
        vec3 ndc = clip.xyz / max(clip.w, 0.05);
        vec2 uv = ndcToUv(ndc.xy);
        if (clip.w <= 0.05 || uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
            break;
        float sceneD = sceneDepthAt(uv);
        float rayD = clipDepthToTexture(ndc.z);
        if (rayD > sceneD && sceneD < 0.99999)
        {
            float behind = length(q - cam) - length(sceneWorld(uv, sceneD) - cam);
            if (behind < stepLen * 2.0 + 0.6)
            {
                hitT = t;
                break;
            }
        }
        prevT = t;
        t += stepLen;
        stepLen *= growth;
    }

    vec4 result = vec4(0.0, 0.0, 0.0, 0.0);
    VV_BRANCH if (hitT > 0.0)
    {
        // Refine between the last two samples.
        float lo = prevT;
        float hi = hitT;
        VV_LOOP for (int k = 0; k < 5; k++)
        {
            float mid = (lo + hi) * 0.5;
            vec4 c2 = mul(u_mvp, vec4(origin + dir * mid, 1.0));
            vec3 n2 = c2.xyz / max(c2.w, 0.05);
            vec2 uv2 = ndcToUv(n2.xy);
            if (clipDepthToTexture(n2.z) > sceneDepthAt(uv2))
                hi = mid;
            else
                lo = mid;
        }
        vec4 cf = mul(u_mvp, vec4(origin + dir * hi, 1.0));
        vec2 uvf = ndcToUv(cf.xy / max(cf.w, 0.05));
        result = vec4(sceneColorAt(uvf), edgeFade(uvf));
    }
    else
    {
        // Missed everything on screen: if the reflected direction points at visible sky, use the
        // rendered sky there (it already has the clouds, sun and moon).
        vec4 far = mul(u_mvp, vec4(origin + dir * 3000.0, 1.0));
        vec2 uv = ndcToUv(far.xy / max(far.w, 0.05));
        if (far.w > 0.05 && uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0 && sceneDepthAt(uv) >= 0.99999)
            result = vec4(sceneColorAt(uv), edgeFade(uv) * 0.999);
    }
    return result;
}

// Wave height: two travelling sines and one noise octave (cheap, smooth, no tiling).
float waveHeight(vec2 p, float t)
{
    float h = sin(dot(p, vec2(0.62, 0.78)) * 1.7 + t * 1.4) * 0.30;
    h += sin(dot(p, vec2(-0.83, 0.41)) * 2.6 - t * 1.9) * 0.18;
    h += valueNoise(p * 1.1 + vec2(t * 0.35, t * 0.22)) * 0.6;
    return h;
}

void main()
{
    vec4 tex = sampleBlock(v_texcoord0, v_layer);
    vec4 color = vec4(tex.rgb * v_color0.rgb, tex.a * v_color0.a);
    vec3 viewDir = normalize(v_rel);
    vec3 n = faceNormal(v_rel);
    color = mix(u_fogColor * vec4(1.0, 1.0, 1.0, color.a), color, v_visibility);
    if (color.a < 0.1)
        discard;

    // Water: upward facing water tiles (or blue-tinted ones when the tile is unknown).
    float up = smoothstep(0.85, 0.98, abs(n.y));
    float tileWater = max(isTile(v_layer, u_tiles.x), isTile(v_layer, u_tiles.z));
    float blueish = u_tiles.x >= 0.0 ? tileWater : step(v_tint.r * 1.25, v_tint.b);
    float isWater = up * blueish;

    vec4 outColor;
    VV_BRANCH if (isWater < 0.5)
    {
        // Glass, ice, slime...: lit like any block, blended normally.
        vec3 lit = vvLit(tex.rgb, v_tint, n, v_rel, v_light, 0.0, 0.0, v_sunLight, v_skyLight);
        outColor = vec4(vvFog(lit, v_fog, v_haze), color.a);
    }
    else
    {
        vec3 world = v_rel + u_camera.xyz;
        float dist = v_fog.x;
        float t = u_time.x;
        bool fromAbove = viewDir.y < 0.0;

        // Smooth wave normal; calmer with distance (which also keeps far water from aliasing).
        vec2 pw = world.xz;
        float e = 0.08;
        float h0 = waveHeight(pw, t);
        float hx = waveHeight(pw + vec2(e, 0.0), t);
        float hz = waveHeight(pw + vec2(0.0, e), t);
        float strength = 0.35 * S_WATER_WAVES * (1.0 - smoothstep(16.0, 96.0, dist) * 0.85);
        vec3 wn = normalize(vec3((h0 - hx) / e * strength, 1.0, (h0 - hz) / e * strength));
        if (!fromAbove)
            wn = -wn;

        float cosV = clamp(dot(-viewDir, wn), 0.0, 1.0);
        float fresnel = clamp(0.04 + 0.96 * vvPow(1.0 - cosV, 4.5), 0.0, 1.0);
        // Reflections use a calmer normal so mirrored terrain stays readable, like Vibrant Visuals.
        vec3 calmN = normalize(mix(vec3(0.0, fromAbove ? 1.0 : -1.0, 0.0), wn, 0.3));
        vec3 R = reflect(viewDir, calmN);
        if (fromAbove)
            R.y = max(abs(R.y), 0.002);

        float skyl = v_light.y;
        float skyVisible = mix(0.15, 1.0, smoothstep(0.35, 0.9, skyl));
        bool scene = u_sceneInfo.x > 0.5;

        // Reflection: screen-space first (skipped where the fresnel makes it invisible), then the
        // analytic sky and clouds only where the trace did not fully cover the pixel.
        vec3 reflection = vec3(0.0, 0.0, 0.0);
        vec4 hit = vec4(0.0, 0.0, 0.0, 0.0);
        float reflQuality = floor(S_REFLECTIONS + 0.5);
        VV_BRANCH if (fromAbove && fresnel > 0.05)
        {
            VV_BRANCH if (scene && reflQuality > 0.5)
            {
                float jitter = hash12(gl_FragCoord.xy + vec2(fract(t * 7.0) * 61.0, 0.0));
                hit = traceReflection(world, R, jitter, reflQuality);
            }
            VV_BRANCH if (hit.a < 0.999)
            {
                vec3 skyR = vvSky(R, false) * skyVisible;
                VV_BRANCH if (S_REFLECTED_CLOUDS > 0.5)
                {
                    vec4 cloud = vvClouds(world, R, 4000.0);
                    skyR = skyR * cloud.a + cloud.rgb * skyVisible;
                }
                reflection = skyR;
            }
            reflection = mix(reflection, hit.rgb, hit.a);
        }

        // Sun/moon glint: a soft core plus gently twinkling sparkles. The glint uses the smooth
        // wave normal, a broad highlight and soft compression so it never turns into harsh,
        // grainy pixels.
        vec3 L = lightDirection();
        vec3 Rg = reflect(viewDir, normalize(mix(vec3(0.0, 1.0, 0.0), wn, 0.7)));
        float rl = max(dot(Rg, L), 0.0);
        float core = vvPow(rl, 260.0) * 2.4 + vvPow(rl, 36.0) * 0.22;
        vec2 cell = floor(world.xz * 8.0);
        float seed = hash12(cell);
        float twinkle = 0.5 + 0.5 * sin(t * (1.5 + seed * 2.0) + seed * 6.2831853);
        twinkle = smoothstep(0.7, 1.0, twinkle) * step(0.88, hash12(cell + 19.7));
        float sparkle = twinkle * vvPow(rl, 24.0) * S_WATER_SPARKLE * (1.0 - smoothstep(12.0, 48.0, dist));
        vec3 lightCol = u_sunDir.y >= 0.0 ? vec3(1.0, 0.95, 0.85) : vec3(0.70, 0.78, 1.0) * 0.5;
        float glintVisible = u_time.y * smoothstep(0.4, 0.8, skyl) * smoothstep(0.0, 0.06, L.y) * (fromAbove ? 1.0 : 0.0);
        float g = (core + sparkle * 0.5) * glintVisible;
        vec3 glint = lightCol * (1.0 - exp(-g * 1.5)) * 1.15;

        // What is under the surface.
        vec3 body;
        float alpha;
        vec3 lightLevel = v_skyLight * skyl * skyl + v_sunLight * 0.25 * skyl;
        vec3 scatterColor = vec3(0.02, 0.17, 0.24) * clamp(luma(lightLevel) * 1.6, 0.03, 1.2);
        scatterColor = vvTonemap(scatterColor * 3.0) * 0.8;
        VV_BRANCH if (scene)
        {
            vec2 uv = gl_FragCoord.xy * u_viewTexel.xy;
            float surfaceDist = length(v_rel);
            vec2 offset = wn.xz * 0.045 * S_REFRACTION * clamp(6.0 / max(surfaceDist, 1.0), 0.15, 1.0);
            vec2 uvR = clamp(uv + offset, vec2_splat(0.001), vec2_splat(0.999));
            float dR = sceneDepthAt(uvR);
            vec3 behindR = sceneWorld(uvR, dR);
            if (dR < 0.99999 && length(behindR - u_camera.xyz) < surfaceDist)
            {
                uvR = uv; // something in front of the water: don't refract it in
                dR = sceneDepthAt(uv);
                behindR = sceneWorld(uv, dR);
            }
            float thickness = dR >= 0.99999 ? 48.0 : max(length(behindR - u_camera.xyz) - surfaceDist, 0.0);
            if (!fromAbove)
                thickness = min(thickness, 6.0) * 0.4;
            vec3 trans = exp(-WATER_EXTINCTION * thickness);
            vec3 below = sceneColorAt(uvR) * vec3(0.82, 1.0, 0.94);
            body = below * trans + scatterColor * (vec3_splat(1.0) - trans);
            alpha = 1.0;
        }
        else
        {
            // No scene copy: vanilla-style water colour, blended.
            vec3 base = vvLit(tex.rgb, v_tint, n, v_rel, v_light, 0.0, 0.0, v_sunLight, v_skyLight);
            body = mix(saturateColor(base, 0.9), scatterColor, 0.45);
            alpha = clamp(mix(color.a, 1.0, fresnel * 0.7), 0.0, 1.0);
        }

        // Looking up from underwater: total internal reflection outside Snell's window.
        if (!fromAbove)
        {
            fresnel = cosV < 0.66 ? 1.0 : fresnel * 0.3;
            reflection = scatterColor;
        }
        else if (fresnel <= 0.05)
            fresnel = 0.0;

        vec3 result = mix(body, reflection, fresnel) + glint;
        alpha = clamp(alpha + luma(glint), 0.0, 1.0);
        result = vvFog(result, v_fog, v_haze);
        outColor = vec4(result, alpha);
    }
    gl_FragColor = outColor;
}
