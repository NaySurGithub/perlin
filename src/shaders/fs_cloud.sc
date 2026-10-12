$input v_color0, v_texcoord0, v_fade

#include "common.sh"

SAMPLER2D(s_sky, 2);

void main()
{
    vec4 texel = texture2D(s_sky, v_texcoord0);
    float alpha = texel.a * v_color0.a * v_fade;
    if (alpha < 0.02)
        discard;
    gl_FragColor = vec4(v_color0.rgb, alpha);
}
