$input v_fog, v_dir

#include "bsl_common.sh"

// Henyey-Greenstein phase function.
float hg(float cosA, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159 * pow(abs(max(1.0 + g2 - 2.0 * g * cosA, 0.0001)), 1.5));
}

// Cloud density at a world position: an fbm field shaped by a cumulus height profile.
float cloudDensity(vec3 p, float base, float thick, float coverage, float t)
{
    float h = (p.y - base) / thick;
    float profile = smoothstep(0.0, 0.18, h) * (1.0 - smoothstep(0.5, 1.0, h));
    vec2 xz = p.xz * 0.0105 + vec2(t * 0.0021, t * 0.0007);
    float n = fbm2(xz);
    // Erode the top and edges with a second, higher frequency layer.
    float detail = valueNoise(xz * 5.3 + vec2(h * 1.7, -t * 0.004));
    float shape = n - coverage + (detail - 0.5) * 0.30 * (0.5 + h);
    return clamp(shape * 3.6, 0.0, 1.0) * profile;
}

void main()
{
    vec3 dir = normalize(v_dir);
    vec3 base = u_modulator.rgb;
    float day = u_sunDir.w * u_time.y;
    bool overworld = u_skyParams.z > 0.5;

    // Gradient: horizon takes the fog colour, the zenith deepens.
    float h = smoothstep(0.0, 0.95, dir.y);
    vec3 zenith = base * mix(vec3(0.92, 0.95, 1.0), vec3(0.55, 0.72, 1.08), day);
    vec3 color = mix(base, zenith, pow(abs(max(h, 0.0)), 0.65));
    vec3 horizonColor = mix(base, u_fogColor.rgb, 0.7);
    color = mix(horizonColor, color, smoothstep(-0.02, 0.35, dir.y));
    color = mix(color, u_fogColor.rgb, linearFog(v_fog.y, 0.0, max(u_fogSky.x, 0.001)) * u_fogColor.a * 0.5);

    if (overworld)
    {
        vec3 sun = u_sunDir.xyz;
        float toSun = max(dot(dir, sun), 0.0);
        float toMoon = max(dot(dir, -sun), 0.0);
        float above = smoothstep(-0.12, 0.05, dir.y);

        // Mie glow around the sun and a low scattering band at dawn and dusk.
        float glow = pow(abs(max(toSun, 0.0)), 6.0) * 0.12 + pow(abs(max(toSun, 0.0)), 60.0) * 0.38 + pow(abs(max(toSun, 0.0)), 800.0) * 1.4;
        color += sunColor() * glow * u_time.y * smoothstep(-0.2, 0.08, sun.y);
        float twilight = (1.0 - smoothstep(0.0, 0.32, sun.y)) * smoothstep(-0.25, 0.0, sun.y);
        float band = exp(-abs(dir.y) * 5.5);
        float side = 0.25 + 0.75 * pow(abs(max(toSun, 0.0)), 2.0);
        color += vec3(1.0, 0.42, 0.14) * band * side * twilight * 0.62 * u_time.y;
        color += vec3(0.30, 0.38, 0.62) * (pow(abs(max(toMoon, 0.0)), 40.0) * 0.28 + pow(abs(max(toMoon, 0.0)), 5.0) * 0.045) * (1.0 - u_sunDir.w) * above;

        // Sun and moon discs (the vanilla quads are discarded in fs_celestial).
        float sunCos = dot(dir, sun);
        float sunDisc = smoothstep(0.99905, 0.99935, sunCos);
        color = mix(color, vec3(1.6, 1.45, 1.15), sunDisc * u_time.y * step(-0.04, dir.y + 0.04));
        vec3 mdir = -sun;
        float moonCos = dot(dir, mdir);
        if (moonCos > 0.9985 && u_sunDir.y < 0.25)
        {
            vec3 mr = normalize(cross(vec3(0.0, 1.0, 0.0), mdir));
            vec3 mu2 = cross(mdir, mr);
            vec2 q = vec2(dot(dir, mr), dot(dir, mu2)) / 0.052;
            float rr = dot(q, q);
            if (rr < 1.0)
            {
                float nz = sqrt(1.0 - rr);
                float a = u_skyParams.y * 0.785398;
                float lit = smoothstep(-0.08, 0.08, q.x * sin(a) + nz * cos(a));
                float crater = valueNoise(q * 4.5 + 3.0) * 0.5 + valueNoise(q * 11.0) * 0.25;
                vec3 moonTex = vec3_splat(0.92 - crater * 0.45);
                vec3 moonShade = mix(vec3(0.03, 0.04, 0.07), moonTex * vec3(0.95, 0.97, 1.05), lit);
                float edge = smoothstep(1.0, 0.93, rr);
                color = mix(color, moonShade, edge * (1.0 - clamp(u_sunDir.w * 3.0, 0.0, 1.0)) * step(-0.04, dir.y + 0.04));
            }
        }

        // Volumetric clouds: a slab raymarched with sun lighting, self shadowing and silver lining.
        float cloudBase = max(u_skyParams.x, 60.0);
        float thick = 34.0;
        float top = cloudBase + thick;
        float camY = u_camera.y;
        float dy = dir.y;
        if (abs(dy) > 0.02 && !(camY > top && dy > 0.0) && !(camY < cloudBase && dy < 0.0))
        {
            float tA = (cloudBase - camY) / dy;
            float tB = (top - camY) / dy;
            float t0 = max(min(tA, tB), 0.0);
            float t1 = max(tA, tB);
            t1 = min(t1, t0 + 900.0);
            if (t1 > t0)
            {
                float rain = 1.0 - u_time.y;
                float coverage = 0.395 - rain * 0.12;
                vec3 L = lightDirection();
                float lightUp = smoothstep(0.0, 0.2, L.y);
                vec3 lightCol = mainLightColor() * lightUp;
                vec3 amb = mix(base, vec3_splat(luma(base) + 0.25), 0.5) * 1.05 + u_fogColor.rgb * 0.2;
                float mu = dot(dir, L);
                float phase = mix(hg(mu, 0.18), hg(mu, 0.72), 0.35) * 4.0 + 0.35;
                float stepLen = (t1 - t0) / 14.0;
                // Interleaved jitter hides the banding of the fixed step count.
                float jitter = hash12(gl_FragCoord.xy + vec2(u_time.x * 7.0, 0.0));
                float trans = 1.0;
                vec3 acc = vec3_splat(0.0);
                for (int i = 0; i < 14; i++)
                {
                    float t = t0 + (float(i) + jitter) * stepLen;
                    vec3 p = vec3(u_camera.x, camY, u_camera.z) + dir * t;
                    float d = cloudDensity(p, cloudBase, thick, coverage, u_time.x);
                    if (d > 0.01)
                    {
                        float hh = clamp((p.y - cloudBase) / thick, 0.0, 1.0);
                        float dl = cloudDensity(p + L * 9.0, cloudBase, thick, coverage, u_time.x);
                        float dl2 = cloudDensity(p + L * 22.0, cloudBase, thick, coverage, u_time.x);
                        float sunT = exp(-(dl * 0.9 + dl2 * 0.8) * 2.2);
                        vec3 light = lightCol * sunT * phase * 1.25;
                        vec3 ambient = amb * mix(0.55, 1.1, hh);
                        float ext = d * stepLen * 0.06;
                        float a = 1.0 - exp(-ext);
                        acc += trans * a * (light + ambient);
                        trans *= 1.0 - a;
                        if (trans < 0.03)
                            break;
                    }
                }
                // Fade into the haze towards the horizon and with distance.
                float fade = smoothstep(0.02, 0.07, abs(dy)) * (1.0 - smoothstep(600.0, 1200.0, t0));
                acc = clamp(acc, 0.0, 4.0);
                acc = acc / (1.0 + acc * 0.35);
                color = mix(color, color * trans + acc, fade);
            }
        }
    }

    gl_FragColor = vec4(color, u_modulator.a);
}
// edit
