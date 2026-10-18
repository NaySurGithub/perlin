$input v_texcoord0, v_layer, v_depth

#include "vv_common.sh"

SAMPLER2D(s_shadow, 2); // the opaque shadow map being built (same light space, twice the size)

// Stained glass, tinted glass, ice, slime and honey seen from the sun: light passing through
// takes their colour. Written into the half-size tint map: rgb = the colour (multiplied over
// every layer of glass), a = depth of the nearest glass (kept with a MIN blend).
void main()
{
    vec4 tex = texture2DArrayLod(s_atlas, vec3(v_texcoord0, v_layer), 0.0);
    float water = max(step(abs(v_layer - u_tiles.x), 0.5), step(abs(v_layer - u_tiles.z), 0.5));
    // Glass behind something solid (from the sun) does not colour anything.
    float opaque = texture2DLod(s_shadow, gl_FragCoord.xy * u_shadowInfo.x * 2.0, 0.0).r;
    if (tex.a < 0.05 || water > 0.5 || v_depth >= opaque)
        discard;
    // Keep the hue at full strength (normalised to the brightest channel) so light comes through
    // richly coloured rather than just darker; more opaque glass colours it more.
    float peak = max(max(tex.r, tex.g), max(tex.b, 0.05));
    vec3 hue = tex.rgb / peak;
    vec3 tint = mix(vec3_splat(1.0), hue, clamp(tex.a * 1.5, 0.0, 0.9));
    gl_FragColor = vec4(tint, v_depth);
}
