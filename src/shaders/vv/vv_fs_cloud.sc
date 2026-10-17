$input v_color0, v_dist, v_dir

#include "vv_common.sh"

// Only used if "vanillaClouds" is turned on; the pack normally draws clouds in the sky shader.
void main()
{
    vec3 body, rim;
    vvCloudColors(body, rim);
    vec3 c = body * mix(0.85, 1.0, v_color0.r) + vvSunGlow(normalize(v_dir)) * 0.5;
    gl_FragColor = vec4(c, v_color0.a * (1.0 - linearFog(v_dist, 0.0, max(u_fogSky.y, 0.001))));
}
