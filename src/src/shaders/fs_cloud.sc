$input v_color0, v_dist

#include "common.sh"

void main()
{
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * (1.0 - linearFog(v_dist, 0.0, u_fogSky.y)));
}
