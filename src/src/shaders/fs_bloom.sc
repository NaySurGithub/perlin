$input v_texcoord0

// Bloom chain: a thresholded downsample, further downsamples, then tent-filtered upsamples that
// are added back level by level (the classic "dual filter" layout).
#include <bgfx_shader.sh>

SAMPLER2D(s_cur, 0);
// x: mode (0 prefilter + downsample, 1 downsample, 2 upsample), y: threshold, z: soft knee,
// w: unused. u_bloomTexel.xy: one texel of the source.
uniform vec4 u_bloomParams;
uniform vec4 u_bloomTexel;

vec3 sampleSrc(vec2 uv)
{
    return texture2DLod(s_cur, uv, 0.0).rgb;
}

vec3 downsample(vec2 uv, vec2 t)
{
    // 13-tap box downsample (Jimenez 2014), weighted to avoid fireflies.
    vec3 a = sampleSrc(uv + t * vec2(-2.0, -2.0));
    vec3 b = sampleSrc(uv + t * vec2(0.0, -2.0));
    vec3 c = sampleSrc(uv + t * vec2(2.0, -2.0));
    vec3 d = sampleSrc(uv + t * vec2(-1.0, -1.0));
    vec3 e = sampleSrc(uv + t * vec2(1.0, -1.0));
    vec3 f = sampleSrc(uv + t * vec2(-2.0, 0.0));
    vec3 g = sampleSrc(uv);
    vec3 h = sampleSrc(uv + t * vec2(2.0, 0.0));
    vec3 i = sampleSrc(uv + t * vec2(-1.0, 1.0));
    vec3 j = sampleSrc(uv + t * vec2(1.0, 1.0));
    vec3 k = sampleSrc(uv + t * vec2(-2.0, 2.0));
    vec3 l = sampleSrc(uv + t * vec2(0.0, 2.0));
    vec3 m = sampleSrc(uv + t * vec2(2.0, 2.0));
    vec3 o = (d + e + i + j) * 0.125;
    o += (a + b + g + f) * 0.03125;
    o += (b + c + h + g) * 0.03125;
    o += (f + g + l + k) * 0.03125;
    o += (g + h + m + l) * 0.03125;
    return o;
}

vec3 upsample(vec2 uv, vec2 t)
{
    // 9-tap tent filter.
    vec3 s = sampleSrc(uv) * 4.0;
    s += (sampleSrc(uv + t * vec2(-1.0, 0.0)) + sampleSrc(uv + t * vec2(1.0, 0.0)) + sampleSrc(uv + t * vec2(0.0, -1.0)) +
          sampleSrc(uv + t * vec2(0.0, 1.0))) * 2.0;
    s += sampleSrc(uv + t * vec2(-1.0, -1.0)) + sampleSrc(uv + t * vec2(1.0, -1.0)) + sampleSrc(uv + t * vec2(-1.0, 1.0)) +
         sampleSrc(uv + t * vec2(1.0, 1.0));
    return s / 16.0;
}

void main()
{
    vec2 t = u_bloomTexel.xy;
    vec3 color;
    if (u_bloomParams.x < 0.5)
    {
        color = downsample(v_texcoord0, t);
        // Soft-knee threshold on brightness: only the bright parts (sun, glints, lights) bloom.
        float br = max(color.r, max(color.g, color.b));
        float knee = max(u_bloomParams.z, 1.0e-4);
        float soft = clamp(br - u_bloomParams.y + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee);
        float contrib = max(soft, br - u_bloomParams.y) / max(br, 1.0e-4);
        color *= contrib;
    }
    else if (u_bloomParams.x < 1.5)
    {
        color = downsample(v_texcoord0, t);
    }
    else
    {
        color = upsample(v_texcoord0, t);
    }
    gl_FragColor = vec4(color, 1.0);
}
