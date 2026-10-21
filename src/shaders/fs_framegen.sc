$input v_texcoord0

// Frame generation for the world: a frame between two real ones is made by moving the last real world image to where the camera
// is expected to be (depth reprojection). Everything that is not world (the HUD, the hand, menus) is whatever differs between
// the real frame with and without them, and stays where it is.
#include <bgfx_shader.sh>

SAMPLER2D(s_world, 0);   // the real frame's world, without HUD
SAMPLER2D(s_depth, 1);   // its depth
SAMPLER2D(s_final, 2);   // the real frame as shown, with HUD
// z: y sign of texture space, w: 1 when clip depth is -1..1
uniform vec4 u_gen;
uniform mat4 u_genInvVp;  // predicted camera: clip -> camera-relative world
uniform mat4 u_genCurVp;  // the real frame's camera: camera-relative world -> clip

vec2 uvOf(vec3 ndc)
{
    return vec2(ndc.x * 0.5 + 0.5, 0.5 + u_gen.z * ndc.y * 0.5);
}

float depthAt(vec2 uv)
{
    float d = texture2DLod(s_depth, clamp(uv, vec2_splat(0.0), vec2_splat(1.0)), 0.0).x;
    return u_gen.w > 0.5 ? d * 2.0 - 1.0 : d;
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
    // Backward reprojection: guess the depth under this pixel from the real frame, find where that point was, look again.
    vec2 ndcXY = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * u_gen.z);
    vec2 src = uv;
    for (int i = 0; i < 3; i++)
    {
        vec4 w = mul(u_genInvVp, vec4(ndcXY, depthAt(src), 1.0));
        w /= w.w;
        vec4 c = mul(u_genCurVp, w);
        src = uvOf(vec3(c.xy / c.w, 0.0));
    }
    gl_FragColor = vec4(texture2DLod(s_world, clamp(src, vec2_splat(0.0), vec2_splat(1.0)), 0.0).rgb, 1.0);
}
