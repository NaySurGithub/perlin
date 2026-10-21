$input v_fog, v_dir

#include "vv_common.sh"

void main()
{
    vec3 dir = normalize(v_dir);
    vec2 cell = floor(vec2(atan2(dir.x, dir.z) * 90.0, dir.y * 180.0));
    float seed = hash12(cell);
    float twinkle = 0.8 + 0.2 * sin(u_time.x * (1.2 + seed * 3.0) + seed * 40.0);
    float dark = 1.0 - smoothstep(-0.12, 0.05, u_sunDir.y);
    vec3 tint = mix(vec3(0.85, 0.90, 1.0), vec3(1.0, 0.95, 0.85), step(0.7, hash12(cell + 7.0)));
    float b = clamp(u_modulator.a * dark * twinkle * u_time.y * S_STARS, 0.0, 1.0);
    gl_FragColor = vec4(tint * b * 1.1, b);
}
