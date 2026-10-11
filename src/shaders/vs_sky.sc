$input a_position
$output v_ndc

#include "common.sh"

void main()
{
    gl_Position = vec4(a_position, 1.0, 1.0);
    v_ndc = a_position;
}
