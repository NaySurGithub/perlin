$input v_color0, v_texcoord0, v_fog, v_layer, v_visibility

#include "common.sh"
#include "block_sampling.sh"

void main()
{
    vec4 color = sampleBlock(v_texcoord0, v_layer) * v_color0;
    color = mix(u_fogColor * vec4(1.0, 1.0, 1.0, color.a), color, v_visibility);
    if (color.a < 0.1)
        discard;
    color.rgb = applyFog(color.rgb, v_fog.x, v_fog.y, u_fogEnv.x, u_fogEnv.y, u_fogEnv.z, u_fogEnv.w, u_fogColor);
    gl_FragColor = color;
}
