$input a_position, a_color0, a_texcoord0, a_texcoord1
$output v_color0, v_texcoord0, v_fog, v_layer, v_visibility, v_rel, v_light, v_tint, v_foliage, v_sunLight, v_skyLight, v_haze

#include "vv_common.sh"

void main()
{
    float layer = floor(a_texcoord1.z * 255.0 + 0.5) + floor(a_texcoord1.w * 255.0 + 0.5) * 256.0;
    float foliage = vvFoliageKind(layer);
    vec3 position = a_position;
    if (S_WAVING > 0.001 && foliage > 0.5)
        position += vvWave(a_position, foliage, a_texcoord0);
    vec3 relative = position - u_camera.xyz;
    gl_Position = mul(u_mvp, vec4(position, 1.0));
    v_foliage = foliage;

    v_fog = vec2(length(relative), max(length(relative.xz), abs(relative.y)));
    vec2 light = floor(a_texcoord1.xy * 255.0 + 0.5);
    if (u_dynParams.x > 0.5)
    {
        vec3 cell = floor(a_position) + vec3_splat(0.5) - u_camera.xyz;
        light.x = max(light.x, floor(dynamicLevel(cell)) * 16.0);
    }
    v_rel = relative;
    v_light = clamp(light / 240.0, 0.0, 1.0);
    v_tint = a_color0.rgb;
    v_color0 = vec4(a_color0.rgb, a_color0.a);
    v_texcoord0 = a_texcoord0;
    v_layer = layer;

    v_sunLight = vvMainLight();
    v_skyLight = vvSkyAmbient();
    v_haze = vvHaze(relative);

    float dist = length(relative);
    v_visibility = mix(1.0, u_chunk.x, clamp((dist - 16.0) / 16.0, 0.0, 1.0));
}
