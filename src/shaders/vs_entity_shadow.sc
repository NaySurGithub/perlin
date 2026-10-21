$input a_position, a_color0, a_texcoord0
$output v_texcoord0, v_depth

#include <bgfx_shader.sh>

uniform mat4 u_mvp;
uniform mat4 u_shadowMtx;

void main()
{
    vec4 world = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_mvp, world);
    v_texcoord0 = a_texcoord0;
    v_depth = mul(u_shadowMtx, world).z;
}
