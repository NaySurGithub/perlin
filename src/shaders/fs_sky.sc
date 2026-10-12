$input v_ndc

#include "common.sh"

void main()
{
    vec4 world = mul(u_inverseViewProjection, vec4(v_ndc, 1.0, 1.0));
    vec3 dir = normalize(world.xyz / world.w - u_camera.xyz);

    float elevation = dir.y;
    vec3 color = mix(u_fogColor.rgb, u_skyColor.rgb, smoothstep(0.0, 0.45, elevation));

    float horizon = 1.0 - smoothstep(0.0, 0.35, abs(elevation));
    vec2 viewFlat = normalize(dir.xz + vec2(0.0001, 0.0));
    vec2 sunFlat = normalize(u_sunDir.xz + vec2(0.0001, 0.0));
    float facing = pow(max(dot(viewFlat, sunFlat), 0.0), 3.0);
    color = mix(color, u_sunrise.rgb, u_sunrise.a * horizon * facing);

    gl_FragColor = vec4(color, 1.0);
}
