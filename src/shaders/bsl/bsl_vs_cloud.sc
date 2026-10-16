$input a_position, a_texcoord0
$output v_color0, v_dist, v_dir

#include "bsl_common.sh"

void main()
{
    vec3 pos = a_position + u_cloudOffset.xyz;
    gl_Position = mul(u_mvp, vec4(pos, 1.0));
    v_dist = length(pos);
    v_dir = pos / max(length(pos), 0.0001);
    v_color0 = vec4(a_texcoord0.x * u_cloudColor.rgb, u_cloudColor.a);
}
