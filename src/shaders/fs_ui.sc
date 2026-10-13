$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_uiTex, 0);
SAMPLER2D(s_uiGlint, 1);
uniform vec4 u_uiParams;  // x: alpha-test cutoff (0 = off), y: 1 = enchantment glint pass, z: glint strength, w: emissive mode
uniform vec4 u_uiOverlay; // rgb: overlay colour (entity hurt flash), a: how much of it is mixed over the texture
uniform vec4 u_uiGlintA;  // glint uv matrix (m00 m01 m10 m11)
uniform vec4 u_uiGlintB;  // glint uv offset

void main()
{
    vec4 texel = texture2D(s_uiTex, v_texcoord0);
    texel.rgb = mix(texel.rgb, u_uiOverlay.rgb, u_uiOverlay.a);
    vec4 color = texel * v_color0;
    if (u_uiParams.w > 1.5)
    {
        // Fullbright (emissive alpha / additive materials): the vertex colour is only the tint, alpha is coverage.
        color = texel * v_color0;
    }
    else if (u_uiParams.w > 0.5)
    {
        // The texture alpha says how emissive each pixel is: mix the lit colour toward white by it.
        color = vec4(texel.rgb * mix(v_color0.rgb, vec3(1.0, 1.0, 1.0), texel.a), v_color0.a);
    }
    if (u_uiParams.x > 0.0 && color.a < u_uiParams.x)
        discard;
    if (u_uiParams.y > 0.5)
    {
        // The item's own alpha is the mask; the glint texture scrolls over it and is added (src^2 + dst, like Java).
        vec2 uv = vec2(dot(u_uiGlintA.xy, v_texcoord0), dot(u_uiGlintA.zw, v_texcoord0)) + u_uiGlintB.xy;
        vec3 g = texture2D(s_uiGlint, uv).rgb * u_uiParams.z;
        float mask = texture2D(s_uiTex, v_texcoord0).a;
        gl_FragColor = vec4(g * g * mask, 0.0);
        return;
    }
    gl_FragColor = color;
}
