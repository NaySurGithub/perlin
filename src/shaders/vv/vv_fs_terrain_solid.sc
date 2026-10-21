$input v_color0, v_texcoord0, v_fog, v_layer, v_visibility, v_rel, v_light, v_tint, v_foliage, v_sunLight, v_skyLight, v_haze

#include "vv_terrain_common.sh"

void main()
{
    vec4 tex = sampleBlock(v_texcoord0, v_layer);
    vec3 viewDir = normalize(v_rel);
    vec3 n = faceNormal(v_rel);
    if (tex.a * v_color0.a < 0.5)
        discard;
    float lava = isTile(v_layer, u_tiles.y);
    float leaves = step(0.5, v_foliage) * step(v_foliage, 1.5);
    vec4 color = vec4(vvLit(tex.rgb, v_tint, n, v_rel, v_light, leaves, lava * 0.9, v_sunLight, v_skyLight), tex.a * v_color0.a);
    color = mix(vec4(vvFogColor(v_haze), color.a), color, v_visibility);
    color.rgb = vvFog(color.rgb, v_fog, v_haze);
    gl_FragColor = color;
}
