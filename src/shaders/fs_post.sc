$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_cur, 0);
SAMPLER2D(s_hist, 1);
SAMPLER2D(s_bloom, 2);
uniform vec4 u_post;
// x: bloom strength (0 = none)
uniform vec4 u_bloom;

void main()
{
    if (u_post.z > 0.01)
    {
        vec2 texel = vec2(1.0 / u_viewRect.z, 1.0 / u_viewRect.w);
        vec2 step = (u_post.w < 0.5 ? vec2(texel.x, 0.0) : vec2(0.0, texel.y)) * u_post.z * 0.25;
        vec3 sum = texture2D(s_cur, v_texcoord0).rgb * 0.2270270270;
        sum += (texture2D(s_cur, v_texcoord0 + step).rgb + texture2D(s_cur, v_texcoord0 - step).rgb) * 0.1945945946;
        sum += (texture2D(s_cur, v_texcoord0 + step * 2.0).rgb + texture2D(s_cur, v_texcoord0 - step * 2.0).rgb) * 0.1216216216;
        sum += (texture2D(s_cur, v_texcoord0 + step * 3.0).rgb + texture2D(s_cur, v_texcoord0 - step * 3.0).rgb) * 0.0540540541;
        sum += (texture2D(s_cur, v_texcoord0 + step * 4.0).rgb + texture2D(s_cur, v_texcoord0 - step * 4.0).rgb) * 0.0162162162;
        gl_FragColor = vec4(sum, 1.0);
        return;
    }
    vec3 cur = texture2D(s_cur, v_texcoord0).rgb;
    vec3 hist = texture2D(s_hist, v_texcoord0).rgb;
    vec3 color = mix(cur, hist, u_post.x);
    if (u_bloom.x > 0.0)
    {
        // Screen-blend the glow so it brightens without clipping to flat white.
        vec3 glow = texture2D(s_bloom, v_texcoord0).rgb * u_bloom.x;
        color = vec3_splat(1.0) - (vec3_splat(1.0) - color) * (vec3_splat(1.0) - clamp(glow, 0.0, 1.0));
    }
    gl_FragColor = vec4(color, 1.0);
}
