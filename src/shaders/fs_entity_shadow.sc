$input v_texcoord0, v_depth

#include <bgfx_shader.sh>

SAMPLER2D(s_uiTex, 0);

void main()
{
    if (texture2D(s_uiTex, v_texcoord0).a < 0.5)
        discard;
    gl_FragColor = vec4(v_depth, 0.0, 0.0, 1.0);
}
