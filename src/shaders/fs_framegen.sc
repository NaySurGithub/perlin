$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_world, 0);
SAMPLER2D(s_depth, 1);
SAMPLER2D(s_final, 2);
uniform vec4 u_gen;
uniform mat4 u_genInvVp;
uniform mat4 u_genCurVp;

vec2 uvOf(vec3 ndc)
{
    return vec2(ndc.x * 0.5 + 0.5, 0.5 + u_gen.z * ndc.y * 0.5);
}

float rawDepth(vec2 uv)
{
    return texture2DLod(s_depth, clamp(uv, vec2_splat(0.0), vec2_splat(1.0)), 0.0).x;
}

float depthAt(vec2 uv)
{
    float d = rawDepth(uv);
    return u_gen.w > 0.5 ? d * 2.0 - 1.0 : d;
}

float linearDepth(float d)
{
    float n = u_gen.x;
    float f = u_gen.y;
    return n * f / (f - d * (f - n));
}

void main()
{
    vec2 uv = v_texcoord0;
    vec3 shown = texture2DLod(s_final, uv, 0.0).rgb;
    vec3 world = texture2DLod(s_world, uv, 0.0).rgb;
    vec3 diff = abs(shown - world);
    if (max(diff.x, max(diff.y, diff.z)) > 0.006)
    {
        gl_FragColor = vec4(shown, 1.0);
        return;
    }
    vec2 ndcXY = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * u_gen.z);
    vec2 src = uv;
    float expected = 1.0;
    for (int i = 0; i < 4; i++)
    {
        vec4 w = mul(u_genInvVp, vec4(ndcXY, depthAt(src), 1.0));
        w /= w.w;
        vec4 c = mul(u_genCurVp, w);
        expected = c.w;
        src = uvOf(vec3(c.xy / max(abs(c.w), 1.0e-5) * sign(c.w), 0.0));
    }
    bool inside = src.x > 0.002 && src.x < 0.998 && src.y > 0.002 && src.y < 0.998;
    float actual = linearDepth(rawDepth(src));
    bool consistent = inside && expected > 0.0 && abs(actual - expected) < 0.05 * expected + 0.12;
    vec2 pick = consistent ? src : uv;
    gl_FragColor = vec4(texture2DLod(s_world, pick, 0.0).rgb, 1.0);
}
