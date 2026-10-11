$input v_ndc

#include "common.sh"

#define SUN_COS 0.9890
#define MOON_COS 0.9950

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

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

    float nightFactor = u_time.z;
    if (nightFactor > 0.001 && elevation > 0.0) {
        vec3 cell = floor(dir * 160.0);
        float star = step(0.9975, hash13(cell)) * nightFactor * 2.0;
        color += vec3(star);
    }

    vec3 sunDir = normalize(u_sunDir.xyz);
    float sunDot = dot(dir, sunDir);
    float moonDot = dot(dir, -sunDir);
    color = mix(color, vec3(1.0, 0.96, 0.82), step(SUN_COS, sunDot) * step(0.0, elevation));
    color = mix(color, vec3(0.85, 0.88, 0.95), step(MOON_COS, moonDot) * step(0.0, elevation));

    gl_FragColor = vec4(color, 1.0);
}
