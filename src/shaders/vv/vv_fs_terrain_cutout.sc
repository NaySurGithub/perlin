$input v_color0, v_texcoord0, v_fog, v_layer, v_visibility, v_rel, v_light, v_tint, v_foliage, v_sunLight, v_skyLight, v_haze

#include "vv_terrain_common.sh"

void main()
{
    vec4 tex = sampleBlock(v_texcoord0, v_layer);
    vec3 viewDir = normalize(v_rel);
    vec3 n = faceNormal(v_rel);
    float alpha = tex.a * v_color0.a;
    if (alpha < 0.5)
        discard;
    vec3 tinted = tex.rgb * v_tint;
    float foliage = max(step(tinted.r * 1.1, tinted.g) * step(tinted.b, tinted.g), step(0.5, v_foliage));
    vec4 color = vec4(vvLit(tex.rgb, v_tint, n, v_rel, v_light, foliage, 0.0, v_sunLight, v_skyLight), alpha);
    color = mix(vec4(vvFogColor(v_haze), color.a), color, v_visibility);
    color.rgb = vvFog(color.rgb, v_fog, v_haze);
    gl_FragColor = color;
}
