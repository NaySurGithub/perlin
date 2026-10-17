$input a_position
$output v_fog, v_dir

#include "vv_common.sh"

void main()
{
    gl_Position = mul(u_mvp, vec4(a_position, 1.0));
    v_fog = vec2(length(a_position), max(length(a_position.xz), abs(a_position.y)));
    v_dir = a_position;
}
