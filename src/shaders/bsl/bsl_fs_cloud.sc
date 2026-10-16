$input v_color0, v_dist, v_dir

#include "bsl_common.sh"

void main()
{
    vec3 dir = normalize(v_dir);
    vec3 c = v_color0.rgb;
    // Sun-lit rims (silver lining) and warm dawn/dusk undersides.
    float toSun = max(dot(dir, u_sunDir.xyz), 0.0);
    float lining = pow(abs(max(toSun, 0.0)), 10.0) * u_sunDir.w * u_time.y;
    float horizon = (1.0 - smoothstep(0.0, 0.45, u_sunDir.y)) * u_sunDir.w;
    c = mix(c, c * sunColor() * 1.15, horizon * 0.55);
    c += sunColor() * lining * 0.28;
    c = saturateColor(c, 1.05);
    gl_FragColor = vec4(c, v_color0.a * (1.0 - linearFog(v_dist, 0.0, max(u_fogSky.y, 0.001))));
}
