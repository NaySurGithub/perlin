$input v_color0

#include "bsl_common.sh"

void main()
{
    // Dawn/dusk glow is part of the sky shader.
    if (u_skyParams.z > 0.5)
        discard;
    if (v_color0.a == 0.0)
        discard;
    gl_FragColor = v_color0 * u_modulator;
}
