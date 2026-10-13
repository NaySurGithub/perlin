$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_cur, 0);
SAMPLER2D(s_hist, 1);
uniform vec4 u_post;

// Motion blur: the new frame mixed with the accumulated previous frames (weight u_post.x; 0 is a plain copy).
void main()
{
    vec3 cur = texture2D(s_cur, v_texcoord0).rgb;
    vec3 hist = texture2D(s_hist, v_texcoord0).rgb;
    gl_FragColor = vec4(mix(cur, hist, u_post.x), 1.0);
}
