#include "../sdk/perlin.h"

static char scratch[256];
static float since;

PERLIN_EXPORT(alloc) uint8_t* alloc(uint32_t size) {
    static uint8_t arena[4096];
    return size <= sizeof(arena) ? arena : 0;
}

PERLIN_EXPORT(on_load) void on_load(void) { perlin_print("speedometer loaded"); }
PERLIN_EXPORT(on_unload) void on_unload(void) { perlin_print("speedometer unloaded"); }
PERLIN_EXPORT(on_connect) void on_connect(void) { perlin_hud("speedometer: connected"); }
PERLIN_EXPORT(on_disconnect) void on_disconnect(void) { perlin_print("disconnected"); }

PERLIN_EXPORT(on_tick) void on_tick(float dt) {
    since += dt;
    if (since < 1.0f) return;
    since = 0;
    double vx = perlin_vel_x(), vz = perlin_vel_z();
    double s2 = vx * vx + vz * vz;
    double s = 0;
    if (s2 > 0) { s = s2; for (int i = 0; i < 20; i++) s = 0.5 * (s + s2 / s); }
    int64_t cmps = (int64_t)(s * 20.0 * 100.0);
    char* p = scratch;
    const char* a = "speed ";
    while (*a) *p++ = *a++;
    p += perlin_itoa(p, cmps / 100);
    *p++ = '.';
    int64_t frac = cmps % 100;
    if (frac < 10) *p++ = '0';
    p += perlin_itoa(p, frac);
    const char* b = " b/s";
    while (*b) *p++ = *b++;
    *p = 0;
    if (perlin_connected()) perlin_hud(scratch);
}

PERLIN_EXPORT(on_chat) void on_chat(const char* text, uint32_t length) {
    if (length >= 4 && text[0] == '!' && text[1] == 'p' && text[2] == 'o' && text[3] == 's') {
        char* p = scratch;
        *p++ = 'x'; *p++ = '=';
        p += perlin_itoa(p, (int64_t)perlin_player_x());
        *p++ = ' '; *p++ = 'y'; *p++ = '=';
        p += perlin_itoa(p, (int64_t)perlin_player_y());
        *p++ = ' '; *p++ = 'z'; *p++ = '=';
        p += perlin_itoa(p, (int64_t)perlin_player_z());
        *p = 0;
        perlin_hud(scratch);
    }
}
