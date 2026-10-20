$input v_texcoord0, v_layer, v_depth

#include "vv_common.sh"

void main()
{
    vec4 tex = texture2DArrayLod(s_atlas, vec3(v_texcoord0, v_layer), 0.0);
    if (tex.a < 0.5)
        discard;
    gl_FragColor = vec4(v_depth, 0.0, 0.0, 1.0);
}
