$input v_fog, v_dir

#include "vv_common.sh"

void main()
{
    vec3 dir = normalize(v_dir);
    vec3 c;
    if (u_skyParams.z < 0.5)
    {
        c = mix(u_fogColor.rgb, u_modulator.rgb, smoothstep(-0.05, 0.6, dir.y));
    }
    else if (u_modulator.a > 0.5 && dot(u_modulator.rgb, vec3_splat(1.0)) < 0.001 && dir.y < 0.0)
    {
        c = vvSkyGradient(vec3(dir.x, 0.0, dir.z)) * mix(0.85, 1.0, smoothstep(-0.4, -0.08, dir.y));
        c += vvSunGlow(normalize(vec3(dir.x, 0.0, dir.z) + vec3(0.0, 1.0e-4, 0.0)));
    }
    else
    {
        c = vvSky(dir, true);
        vec4 cloud = vvClouds(u_camera.xyz, dir, 6000.0);
        c = c * cloud.a + cloud.rgb;
    }
    float underwater = vvUnderwater();
    vec3 waterCol = vvUnderwaterColor();
    c = mix(c, mix(waterCol, c * vec3(0.55, 0.85, 0.95), smoothstep(0.55, 0.8, dir.y)), underwater);
    gl_FragColor = vec4(c, u_modulator.a);
}
