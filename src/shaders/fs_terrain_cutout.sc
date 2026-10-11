$input v_color0, v_texcoord0, v_texcoord1, v_fogDistance, v_layer

#include "common.sh"

void main()
{
    vec4 texel = texture2DArray(s_atlas, vec3(v_texcoord0, v_layer));
    if (texel.a < 0.1)
        discard;
    vec3 color = texel.rgb * v_color0.rgb * lightmapColor(v_texcoord1);
    gl_FragColor = vec4(applyFog(color, v_fogDistance), 1.0);
}
