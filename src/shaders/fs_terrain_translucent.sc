$input v_color0, v_texcoord0, v_texcoord1, v_fogDistance

#include "common.sh"

void main()
{
    vec4 texel = texture2D(s_atlas, v_texcoord0) * v_color0;
    if (texel.a < 0.01)
        discard;
    vec3 color = texel.rgb * lightmapColor(v_texcoord1);
    gl_FragColor = vec4(applyFog(color, v_fogDistance), texel.a);
}
