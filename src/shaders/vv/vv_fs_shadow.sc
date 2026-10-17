$input v_texcoord0, v_layer, v_depth

#include "vv_common.sh"

void main()
{
    gl_FragColor = vec4(v_depth, 0.0, 0.0, 1.0);
}
