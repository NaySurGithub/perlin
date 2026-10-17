$input v_color0, v_texcoord0, v_fog, v_layer, v_visibility, v_rel, v_light, v_tint

#include "vv_terrain_common.sh"

void main()
{
    vec4 tex = sampleBlock(v_texcoord0, v_layer);
    vec3 viewDir = normalize(v_rel);
    vec3 n = faceNormal(v_rel);
    float lava = isTile(v_layer, u_tiles.y);
    vec4 color = vec4(vvLit(tex.rgb, v_tint, n, v_rel, v_light, 0.0, lava * 0.9), tex.a * v_color0.a);
    color = mix(u_fogColor * vec4(1.0, 1.0, 1.0, color.a), color, v_visibility);
    color.rgb = vvFog(color.rgb, v_fog, viewDir);
    gl_FragColor = color;
}
