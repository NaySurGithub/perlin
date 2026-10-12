$input v_color0, v_texcoord0, v_fade

#include "common.sh"

SAMPLER2D(s_sky, 2);

void main()
{
    gl_FragColor = texture2D(s_sky, v_texcoord0) * v_color0 * u_skyObject;
}
