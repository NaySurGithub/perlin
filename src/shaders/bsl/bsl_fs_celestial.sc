$input v_texcoord0

#include "bsl_common.sh"

SAMPLER2D(s_sky, 2);

void main()
{
    // In the overworld the sky shader draws the sun and moon itself so clouds can pass in front of them.
    if (u_skyParams.z > 0.5)
        discard;
    vec4 color = texture2D(s_sky, v_texcoord0);
    if (color.a == 0.0)
        discard;
    gl_FragColor = color * u_modulator;
}
