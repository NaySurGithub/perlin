$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_uiTex, 0);
SAMPLER2D(s_uiGlint, 1);
uniform vec4 u_uiParams;
uniform vec4 u_uiOverlay;
uniform vec4 u_uiGlintA;
uniform vec4 u_uiGlintB;

void main()
{
    vec4 texel = texture2D(s_uiTex, v_texcoord0);
    texel.rgb = mix(texel.rgb, u_uiOverlay.rgb, u_uiOverlay.a);
    vec4 color = texel * v_color0;
    if (u_uiParams.w > 1.5)
    {
        color = texel * v_color0;
    }
    else if (u_uiParams.w > 0.5)
    {
        color = vec4(texel.rgb * mix(v_color0.rgb, vec3(1.0, 1.0, 1.0), texel.a), v_color0.a);
    }
    if (u_uiParams.x > 0.0 && color.a < u_uiParams.x)
        discard;
    if (u_uiParams.y > 0.5)
    {
        vec2 uv = vec2(dot(u_uiGlintA.xy, v_texcoord0), dot(u_uiGlintA.zw, v_texcoord0)) + u_uiGlintB.xy;
        vec3 g = texture2D(s_uiGlint, uv).rgb * u_uiParams.z;
        float mask = texture2D(s_uiTex, v_texcoord0).a;
        gl_FragColor = vec4(g * g * mask, 0.0);
        return;
    }
    gl_FragColor = color;
}
