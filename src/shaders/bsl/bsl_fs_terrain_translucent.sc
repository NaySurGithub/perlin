$input v_color0, v_texcoord0, v_fog, v_layer, v_visibility, v_rel, v_light, v_tint

#include "bsl_terrain_common.sh"

// Two scrolling octaves of value noise form the wave height field; its gradient perturbs the normal.
float waveHeight(vec2 p, float t)
{
    float h = valueNoise(p * 0.9 + vec2(t * 0.35, t * 0.20));
    h += valueNoise(p * 2.1 - vec2(t * 0.40, -t * 0.55)) * 0.5;
    h += valueNoise(p * 4.7 + vec2(-t * 0.8, t * 0.6)) * 0.22;
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


    // Water: upward facing, blue dominant after tinting.
    float up = smoothstep(0.85, 0.98, n.y);
    float tileWater = max(isTile(v_layer, u_tiles.x), isTile(v_layer, u_tiles.z));
    float blueish = u_tiles.x >= 0.0 ? tileWater : step(v_tint.r * 1.25, v_tint.b);
    float isWater = up * blueish;

    vec3 base = bslLit(tex.rgb, v_tint, n, v_rel, v_light, 0.0, 0.0);
    float alpha = color.a;

    if (isWater > 0.5)
    {
        vec3 world = v_rel + u_camera.xyz;
        float t = u_time.x;
        float e = 0.08;
        float h0 = waveHeight(world.xz, t);
        float hx = waveHeight(world.xz + vec2(e, 0.0), t);
        float hz = waveHeight(world.xz + vec2(0.0, e), t);
        // Ripples fade with distance so the far water stays calm and free of shimmer.
        float calm = 1.0 - smoothstep(24.0, 90.0, v_fog.x);
        vec3 wn = normalize(vec3((h0 - hx) * 0.55 * calm, 1.0, (h0 - hz) * 0.55 * calm));

        vec3 r = reflect(viewDir, wn);
        r.y = abs(r.y);
        float cosT = clamp(dot(-viewDir, wn), 0.0, 1.0);
        float fresnel = 0.03 + 0.97 * pow(abs(clamp(1.0 - cosT, 0.0, 1.0)), 5.0);

        // Reflected sky: horizon colour rising to a deeper zenith.
        vec3 zenith = u_fogColor.rgb * vec3(0.62, 0.78, 1.05);
        vec3 sky = mix(u_fogColor.rgb, zenith, smoothstep(0.0, 0.75, r.y));
        sky *= mix(0.25, 1.0, max(v_light.y, 0.12));
        // Clouds in the mirror: a soft brighter band that drifts with the waves.
        float cloud = smoothstep(0.55, 0.85, valueNoise(r.xz / (r.y + 0.25) * 1.3 + vec2(t * 0.02, 0.0)));
        sky = mix(sky, vec3_splat(luma(sky) * 1.25 + 0.08), cloud * 0.35 * u_sunDir.w);

        // Sun and moon glints.
        float sunSpec = pow(abs(max(dot(r, u_sunDir.xyz), 0.0)), 700.0) * 14.0 + pow(abs(max(dot(r, u_sunDir.xyz), 0.0)), 38.0) * 0.45;
        float moonSpec = pow(abs(max(dot(r, -u_sunDir.xyz), 0.0)), 500.0) * 4.0;
        vec3 spec = (sunColor() * sunSpec * u_sunDir.w + vec3(0.65, 0.78, 1.0) * moonSpec * (1.0 - u_sunDir.w)) *
                    u_time.y * step(0.4, v_light.y);

        // Gentler body colour than the raw tile, with depth-like darkening toward the sides.
        vec3 body = saturateColor(base, 0.86);
        body *= 0.92 + 0.12 * wn.y;
        base = mix(body, sky, clamp(fresnel * 0.85, 0.0, 0.9)) + spec;
        alpha = clamp(mix(alpha, 1.0, fresnel * 0.65) + luma(spec) * 0.8, 0.0, 1.0);
    }

    color.rgb = bslFog(base, v_fog, viewDir);
    color.a = alpha;
    gl_FragColor = color;
}
