$input v_fog

#include "common.sh"

void main()
{
    vec3 color = applyFog(u_modulator.rgb, v_fog.x, v_fog.y, 0.0, u_fogSky.x, u_fogSky.x, u_fogSky.x, u_fogColor);
    gl_FragColor = vec4(color, u_modulator.a);
}
