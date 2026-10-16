#include "bsl_common.sh"
#include "bsl_block_sampling.sh"

SAMPLER2D(s_shadow, 2);

vec3 faceNormal(vec3 rel)
{
    vec3 n = cross(dFdx(rel), dFdy(rel));
    float l = length(n);
    return l > 1.0e-8 ? n / l : vec3(0.0, 1.0, 0.0);
}

float vanillaShade(vec3 n)
{
    float up = max(n.y, 0.0);
    float down = max(-n.y, 0.0);
    float z = abs(n.z);
    float x = abs(n.x);
    return clamp(up * 1.0 + down * 0.5 + z * 0.8 + x * 0.6, 0.4, 1.0);
}

float sunShadow(vec3 world, vec3 n, float ndl)
{
    if (u_shadowInfo.w < 0.5)
        return -1.0;
    float texelWorld = u_shadowInfo.y * u_shadowInfo.x;
    vec3 p = world + n * (texelWorld * 1.4 + 0.02);
    vec3 sc = mul(u_shadowMtx, vec4(p, 1.0)).xyz;
    vec2 edge = min(sc.xy, vec2(1.0, 1.0) - sc.xy);
    float inside = smoothstep(0.0, 0.06, min(edge.x, edge.y));
    float bias = (texelWorld * 1.6 * (1.0 - ndl) + 0.05) / u_shadowInfo.z;
    float lit = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 uv = sc.xy + vec2(float(x), float(y)) * u_shadowInfo.x * 1.1;
            float stored = texture2DLod(s_shadow, uv, 0.0).r;
            lit += (sc.z - bias <= stored) ? 1.0 : 0.0;
        }
    }
    lit /= 9.0;
    return mix(1.0, lit, inside);
}

vec3 bslLit(vec3 texRgb, vec3 tint, vec3 n, vec3 rel, vec2 light, float backlight, float emissive)
{
    float shade = vanillaShade(n);
    vec3 biomeAo = clamp(tint / max(shade, 0.01), 0.0, 1.5);
    vec3 albedo = pow(clamp(texRgb * biomeAo, 0.0, 1.0), vec3_splat(2.2));
    vec3 viewDir = normalize(rel);
    float dist = length(rel);
    float overworld = u_skyParams.z;
    float skyl = light.y;
    float skylSqr = skyl * skyl;
    vec3 L = lightDirection();
    float ndl = dot(n, L);
    float up = smoothstep(0.0, 0.1, L.y);
    float shadow = 0.0;
    float direct = 0.0;
    float scatter = 0.0;
    if (overworld > 0.5)
    {
        float sky = smoothstep(0.35, 0.95, skyl);
        if (ndl > 0.0 || backlight > 0.0)
        {
            float s = sunShadow(rel + u_camera.xyz, n, max(ndl, 0.0));
            if (s < 0.0)
                s = sky;
            shadow = s * sky;
        }
        float VoL = clamp(dot(viewDir, L) * 0.5 + 0.5, 0.0, 1.0);
        scatter = pow(VoL, 16.0) * (1.0 - bslRain()) * backlight * S_FOLIAGE;
        float nol = clamp(ndl * 1.01 - 0.01, 0.0, 1.0);
        nol = mix(nol, 1.0, sqrt(backlight) * 0.7 * clamp(S_FOLIAGE, 0.0, 1.0));
        nol = mix(nol, 1.0, scatter);
        direct = nol * shadow * up;
    }
    vec3 lightCol = bslLightColor();
    vec3 ambientCol = bslAmbientColor();
    float shadowMult = 1.0 - 0.95 * bslRain();
    vec3 scene = mix(ambientCol * skyl, lightCol, direct * shadowMult);
    scene *= skylSqr * (1.0 + scatter * shadow);
    scene *= overworld > 0.5 ? 1.0 : 0.0;
    scene += vec3(0.07, 0.04, 0.05) * (1.0 - overworld) * S_AMBIENT;

    float bl = light.x;
    float held = u_heldLight.x / 15.0;
    if (held > 0.001)
    {
        float reach = 3.0 + held * 11.0;
        float att = clamp(1.0 - dist / reach, 0.0, 1.0);
        bl = max(bl, att * att * held * (0.35 + 0.65 * max(dot(n, -viewDir), 0.0)));
    }
    float newLight = pow(bl, 10.0) * 1.6 + bl * 0.6;
    vec3 block = bslBlockColor() * newLight * newLight;
    vec3 minLight = vec3_splat(0.0025) * (1.0 - skylSqr);
    vec3 albedoNorm = normalize(albedo + 0.00001);
    vec3 emissiveLight = mix(albedoNorm, vec3_splat(1.0), emissive * 0.5) * emissive * 2.0;
    float flatten = clamp(1.0 - pow(1.0 - clamp(emissive, 0.0, 1.0), 128.0), 0.0, 1.0);
    float diffuse = mix(shade, 1.0, flatten);
    vec3 total = max(scene + block + emissiveLight + minLight, vec3_splat(0.0));
    vec3 hdr = albedo * total * diffuse;
    return bslFinish(hdr);
}

vec3 bslFog(vec3 color, vec2 fogDistance, vec3 viewDir)
{
    float f = fogAmount(fogDistance.x, fogDistance.y);
    float toSun = max(dot(viewDir, u_sunDir.xyz), 0.0);
    float scatter = (pow(toSun, 6.0) * 0.22 + pow(toSun, 36.0) * 0.35) * u_sunDir.w * u_time.y;
    vec3 fogTint = u_fogColor.rgb + sunColor() * scatter * f * f * (1.0 - u_fogColor.rgb * 0.5);
    return mix(color, fogTint, f);
}

float isTile(float layer, float tile)
{
    return abs(layer - tile) < 0.5 ? 1.0 : 0.0;
}
