$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0, v_fade

#include "common.sh"

void main()
{
    vec3 relative = vec3((a_position.x - u_cloud.x) * 12.0, a_position.y + u_cloud.z, (a_position.z - u_cloud.y) * 12.0);
    gl_Position = mul(u_mvp, vec4(relative, 1.0));
    v_color0 = vec4(a_color0.rgb * u_cloudColor.rgb, a_color0.a);
    v_texcoord0 = a_texcoord0 + u_cloudUv.xy;
    v_fade = 1.0 - smoothstep(u_cloudColor.w * 0.5, u_cloudColor.w * 0.95, length(relative.xz));
}
