$input v_color0, v_texcoord0, v_fog, v_layer, v_visibility, v_rel, v_light, v_tint

#include "bsl_terrain_common.sh"

void main()
{
    vec4 tex = sampleBlock(v_texcoord0, v_layer);
    vec3 viewDir = normalize(v_rel);
    vec3 n = faceNormal(v_rel);
    float alpha = tex.a * v_color0.a;
    if (alpha < 0.5)
        discard;
    float foliage = step(tex.r * 1.15, tex.g) * step(tex.b, tex.g);
    vec4 color = vec4(bslLit(tex.rgb, v_tint, n, v_rel, v_light, foliage, 0.0), alpha);
    color = mix(u_fogColor * vec4(1.0, 1.0, 1.0, color.a), color, v_visibility);
    color.rgb = bslFog(color.rgb, v_fog, viewDir);
    gl_FragColor = color;
}
