$input a_position, a_texcoord0, a_texcoord1
$output v_texcoord0, v_layer, v_depth

#include "vv_common.sh"

void main()
{
    float layer = floor(a_texcoord1.z * 255.0 + 0.5) + floor(a_texcoord1.w * 255.0 + 0.5) * 256.0;
    vec3 position = a_position;
    gl_Position = mul(u_mvp, vec4(position, 1.0));
    v_texcoord0 = a_texcoord0;
    v_layer = layer;
    v_depth = mul(u_shadowMtx, vec4(position, 1.0)).z;
}
