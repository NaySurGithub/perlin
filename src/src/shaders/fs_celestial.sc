$input v_texcoord0

#include "common.sh"

SAMPLER2D(s_sky, 2);

void main()
{
    vec4 color = texture2D(s_sky, v_texcoord0);
    if (color.a == 0.0)
        discard;
    gl_FragColor = color * u_modulator;
}
