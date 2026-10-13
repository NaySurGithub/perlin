$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

uniform vec4 u_post; // x: weight of the previous frame, y: 1 when render targets have their origin at the bottom left

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    vec2 uv = a_position.xy * 0.5 + 0.5;
    uv.y = u_post.y > 0.5 ? uv.y : 1.0 - uv.y;
    v_texcoord0 = uv;
}
