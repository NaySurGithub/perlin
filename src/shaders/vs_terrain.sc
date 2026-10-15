$input a_position, a_color0, a_texcoord0, a_texcoord1
$output v_color0, v_texcoord0, v_fog, v_layer, v_visibility, v_rel, v_light, v_tint

#include "common.sh"

void main()
{
    vec3 relative = a_position - u_camera.xyz;
    gl_Position = mul(u_mvp, vec4(a_position, 1.0));

    v_fog = vec2(length(relative), max(length(relative.xz), abs(relative.y)));
    vec2 light = floor(a_texcoord1.xy * 255.0 + 0.5);
    if (u_dynParams.x > 0.5 && u_dynParams.x < 1.5)
    {
        vec3 cell = floor(a_position) + vec3_splat(0.5) - u_camera.xyz;
        light.x = max(light.x, floor(dynamicLevel(cell)) * 16.0);
    }
    v_rel = relative;
    v_light = light;
    v_tint = a_color0.rgb;
    v_color0 = vec4(a_color0.rgb * sampleLightmap(light), a_color0.a);
    v_texcoord0 = a_texcoord0;
    v_layer = floor(a_texcoord1.z * 255.0 + 0.5) + floor(a_texcoord1.w * 255.0 + 0.5) * 256.0;

    float dist = length(relative);
    v_visibility = mix(1.0, u_chunk.x, clamp((dist - 16.0) / 16.0, 0.0, 1.0));
}
