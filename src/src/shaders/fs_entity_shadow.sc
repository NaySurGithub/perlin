$input v_texcoord0, v_depth

#include <bgfx_shader.sh>

SAMPLER2D(s_uiTex, 0);

// Mobs and players only: the nearest depth along the light is all this map holds (the depth test keeps
// the nearest fragment and the colour stores it); the terrain shader looks it up next to the terrain map.
void main()
{
    if (texture2D(s_uiTex, v_texcoord0).a < 0.5)
        discard;
    gl_FragColor = vec4(v_depth, 0.0, 0.0, 1.0);
}
