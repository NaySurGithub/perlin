$input v_texcoord0, v_depth

#include <bgfx_shader.sh>

SAMPLER2D(s_uiTex, 0);
SAMPLER2D(s_shadow, 2);    // the cached terrain shadow map this one was copied from
uniform vec4 u_shadowInfo; // x: 1 / map size

void main()
{
    if (texture2D(s_uiTex, v_texcoord0).a < 0.5)
        discard;
    // Keep the terrain where it is nearer the sun (this map's own depth buffer only holds mobs).
    float terrain = texture2DLod(s_shadow, gl_FragCoord.xy * u_shadowInfo.x, 0.0).r;
    if (terrain <= v_depth)
        discard;
    gl_FragColor = vec4(v_depth, 0.0, 0.0, 1.0);
}
