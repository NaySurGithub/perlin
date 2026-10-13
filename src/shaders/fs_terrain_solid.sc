$input v_color0, v_texcoord0, v_fog, v_layer, v_visibility, v_rel, v_light, v_tint

#include "common.sh"
#include "terrain_light.sh"
#include "block_sampling.sh"

void main()
{
    vec4 color = sampleBlock(v_texcoord0, v_layer) * litColor(v_color0, v_rel, v_light, v_tint);
    color = mix(u_fogColor * vec4(1.0, 1.0, 1.0, color.a), color, v_visibility);
    // The atlas has holey tiles (leaves, grass-side bases) that Java renders in cutout layers.
    if (color.a < 0.5)
        discard;
    color.rgb = applyFog(color.rgb, v_fog.x, v_fog.y, u_fogEnv.x, u_fogEnv.y, u_fogEnv.z, u_fogEnv.w, u_fogColor);
    gl_FragColor = color;
}
