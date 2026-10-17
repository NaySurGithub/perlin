$input v_fog, v_dir

#include "vv_common.sh"

void main()
{
    vec3 dir = normalize(v_dir);
    if (u_skyParams.z < 0.5)
    {
        // Nether / End: the vanilla colours with a soft gradient.
        vec3 base = u_modulator.rgb;
        vec3 c = mix(u_fogColor.rgb, base, smoothstep(-0.05, 0.6, dir.y));
        gl_FragColor = vec4(c, u_modulator.a);
        return;
    }
    // Vanilla draws a dark disc under the horizon with modulator = black; keep it dark.
    if (u_modulator.a > 0.5 && dot(u_modulator.rgb, vec3_splat(1.0)) < 0.001 && dir.y < 0.0)
    {
        gl_FragColor = vec4(vvSkyGradient(vec3(dir.x, 0.0, dir.z)) * 0.85, 1.0);
        return;
    }
    vec3 c = vvSky(dir, true);
    vec4 cloud = vvClouds(u_camera.xyz, dir, 6000.0);
    c = c * cloud.a + cloud.rgb;
    gl_FragColor = vec4(c, u_modulator.a);
}
