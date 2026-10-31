#include <stdint.h>
#include <stdlib.h>
#include <string.h>

__attribute__((import_module("perlin"), import_name("call"))) int32_t perlin_call(uint32_t op, const void* in,
                                                                                  uint32_t in_len, void* out,
                                                                                  uint32_t out_cap);

__attribute__((export_name("perlin_alloc"))) void* perlin_alloc(uint32_t n) {
    return malloc(n ? n : 1);
}
__attribute__((export_name("perlin_free"))) void perlin_free(void* p) {
    free(p);
}

__attribute__((export_name("perlin_subscriptions"))) uint32_t perlin_subscriptions(void) {
    return 1u << 7;
}

static void local_message(const char* text) {
    uint32_t n = (uint32_t)strlen(text);
    uint8_t buf[512];
    if (n > 500)
        n = 500;
    memcpy(buf, &n, 4);
    memcpy(buf + 4, text, n);
    uint8_t out[16];
    perlin_call(0x0702, buf, 4 + n, out, sizeof out);
}

__attribute__((export_name("perlin_event"))) uint32_t perlin_event(uint32_t id, const uint8_t* data,
                                                                   uint32_t len) {
    (void)data;
    (void)len;
    if (id == 7)
        local_message("hello from a C plugin");
    return 0;
}
