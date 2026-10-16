$input v_fog, v_dir

#include "bsl_common.sh"

void main()
{
    vec3 dir = normalize(v_dir);
    vec2 cell = floor(vec2(atan2(dir.x, dir.z) * 90.0, dir.y * 180.0));
    float seed = hash12(cell);
    float twinkle = 0.65 + 0.35 * sin(u_time.x * (1.5 + seed * 4.0) + seed * 40.0);
    vec3 tint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.88, 0.7), step(0.6, hash12(cell + 7.0)));
    gl_FragColor = vec4(u_modulator.rgb * tint * twinkle * 1.2, u_modulator.a);
}
