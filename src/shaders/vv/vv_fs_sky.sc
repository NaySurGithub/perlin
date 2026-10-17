$input v_fog, v_dir

#include "vv_common.sh"

void main()
{
    vec3 dir = normalize(v_dir);
    vec3 c;
    if (u_skyParams.z < 0.5)
    {
        // Nether / End: the vanilla colours with a soft gradient.
        c = mix(u_fogColor.rgb, u_modulator.rgb, smoothstep(-0.05, 0.6, dir.y));
    }
    else if (u_modulator.a > 0.5 && dot(u_modulator.rgb, vec3_splat(1.0)) < 0.001 && dir.y < 0.0)
    {
        // Vanilla's dark disc under the horizon (modulator = black): keep it dark.
        c = vvSkyGradient(vec3(dir.x, 0.0, dir.z)) * 0.85;
    }
    else
    {
        c = vvSky(dir, true);
        vec4 cloud = vvClouds(u_camera.xyz, dir, 6000.0);
        c = c * cloud.a + cloud.rgb;
    }
    gl_FragColor = vec4(c, u_modulator.a);
}
