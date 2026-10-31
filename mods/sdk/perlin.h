#pragma once
#include <stdint.h>

/* every call and event id, from the shared table (perlin/abi_table.h) */
#include "perlin/abi_table.h"
#define PERLIN_C_OP(Name, Id, ...) PERLIN_OP_##Name = Id,
#define PERLIN_C_EVENT(Name, Id, ...) PERLIN_EVENT_##Name = Id,
enum PerlinOp { PERLIN_ABI_OPS(PERLIN_C_OP) };
enum PerlinEvent { PERLIN_ABI_EVENTS(PERLIN_C_EVENT) };
#undef PERLIN_C_OP
#undef PERLIN_C_EVENT

/* the one host function every call goes through: >= 0 bytes written to out, < 0 a status
 * (below -16: out was too small, -(16 + needed)) */
#if defined(__wasm__)
__attribute__((import_module("perlin"), import_name("call")))
#endif
int32_t
perlin_call(uint32_t op, const void* in, uint32_t in_len, void* out, uint32_t out_cap);

#define PERLIN_IMPORT(name) __attribute__((import_module("perlin"), import_name(#name)))
#define PERLIN_EXPORT(name) __attribute__((export_name(#name)))

PERLIN_IMPORT(log) void perlin_log(const char* text, uint32_t length);
PERLIN_IMPORT(chat) void perlin_chat(const char* text, uint32_t length);
PERLIN_IMPORT(hud_text) void perlin_hud_text(const char* text, uint32_t length);
PERLIN_IMPORT(player_x) double perlin_player_x(void);
PERLIN_IMPORT(player_y) double perlin_player_y(void);
PERLIN_IMPORT(player_z) double perlin_player_z(void);
PERLIN_IMPORT(player_yaw) float perlin_player_yaw(void);
PERLIN_IMPORT(player_pitch) float perlin_player_pitch(void);
PERLIN_IMPORT(game_mode) int32_t perlin_game_mode(void);
PERLIN_IMPORT(connected) int32_t perlin_connected(void);
PERLIN_IMPORT(time_ms) int64_t perlin_time_ms(void);
PERLIN_IMPORT(vel_x) double perlin_vel_x(void);
PERLIN_IMPORT(vel_y) double perlin_vel_y(void);
PERLIN_IMPORT(vel_z) double perlin_vel_z(void);
PERLIN_IMPORT(on_ground) int32_t perlin_on_ground(void);
PERLIN_IMPORT(sprinting) int32_t perlin_sprinting(void);
PERLIN_IMPORT(sneaking) int32_t perlin_sneaking(void);

static inline uint32_t perlin_strlen(const char* s) {
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static inline void perlin_say(const char* s) {
    perlin_chat(s, perlin_strlen(s));
}
static inline void perlin_hud(const char* s) {
    perlin_hud_text(s, perlin_strlen(s));
}
static inline void perlin_print(const char* s) {
    perlin_log(s, perlin_strlen(s));
}

static inline uint32_t perlin_itoa(char* out, int64_t v) {
    char tmp[24];
    uint32_t n = 0, len = 0;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
    do {
        tmp[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (neg)
        out[len++] = '-';
    while (n)
        out[len++] = tmp[--n];
    out[len] = 0;
    return len;
}
