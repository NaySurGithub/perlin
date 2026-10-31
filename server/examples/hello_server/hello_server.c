/* smallest useful server plugin, in freestanding c against the raw abi (no libc, no sdk):
 *   - welcomes players on join
 *   - cancels chat that contains "creeper"
 *   - registers /hello, which answers with the online player count
 * C++ plugins get all of this typed from <perlin/perlin.hpp>; ids come from perlin/abi_table.h either way.
 * build: server/examples/hello_server/build.sh
 */
#include "perlin.h"

typedef uint32_t u32;
typedef unsigned long long u64;

/* bump allocator for event payloads: reset on every event, plugins never keep them */
static unsigned char heap[1 << 16];
static u32 heap_top;

__attribute__((export_name("perlin_alloc"))) void* perlin_alloc(u32 size) {
    if (heap_top + size > sizeof heap)
        heap_top = 0;
    void* p = heap + heap_top;
    heap_top += (size + 7u) & ~7u;
    return p;
}
__attribute__((export_name("perlin_free"))) void perlin_free(void* p) {
    (void)p;
}

void* memcpy(void* d, const void* s, unsigned long n) {
    unsigned char* a = d;
    const unsigned char* b = s;
    while (n--)
        *a++ = *b++;
    return d;
}

/* payloads: little endian, a string is u32 length + bytes, a struct is its fields in order */
static unsigned char out[512];
static u32 outn;
static void w_u32(u32 v) {
    for (int i = 0; i < 4; i++)
        out[outn++] = (unsigned char)(v >> (8 * i));
}
static void w_u64(u64 v) {
    for (int i = 0; i < 8; i++)
        out[outn++] = (unsigned char)(v >> (8 * i));
}
static void w_str(const char* s, u32 n) {
    w_u32(n);
    for (u32 i = 0; i < n; i++)
        out[outn++] = (unsigned char)s[i];
}
static u32 r_u32(const unsigned char** p) {
    u32 v = 0;
    for (int i = 0; i < 4; i++)
        v |= (u32)(*p)[i] << (8 * i);
    *p += 4;
    return v;
}
static u64 r_u64(const unsigned char** p) {
    u64 v = 0;
    for (int i = 0; i < 8; i++)
        v |= (u64)(*p)[i] << (8 * i);
    *p += 8;
    return v;
}

static void say(u64 player, const char* text, u32 n) {
    outn = 0;
    w_u64(player);
    w_str(text, n);
    perlin_call(PERLIN_OP_PlayerMessage, out, outn, 0, 0);
}

static int contains_creeper(const char* s, u32 n) {
    static const char word[] = "creeper";
    for (u32 i = 0; i + 7 <= n; i++) {
        u32 j = 0;
        while (j < 7 && (s[i + j] | 0x20) == word[j])
            j++;
        if (j == 7)
            return 1;
    }
    return 0;
}

/* one 64-bit mask: low word here, high word below */
#define BIT(e) (1ull << (e))
static const u64 kWanted =
    BIT(PERLIN_EVENT_PlayerJoin) | BIT(PERLIN_EVENT_PlayerChat) | BIT(PERLIN_EVENT_Command);
__attribute__((export_name("perlin_subscriptions"))) u32 perlin_subscriptions(void) {
    return (u32)kWanted;
}
__attribute__((export_name("perlin_subscriptions_hi"))) u32 perlin_subscriptions_hi(void) {
    return (u32)(kWanted >> 32);
}

__attribute__((export_name("perlin_event"))) u32 perlin_event(u32 id, const unsigned char* p, u32 len) {
    heap_top = 0;
    (void)len;
    if (id == PERLIN_EVENT_Load) {
        /* name, usage, description */
        outn = 0;
        w_str("hello", 5);
        w_str("/hello", 6);
        w_str("says hello", 10);
        perlin_call(PERLIN_OP_CommandRegister, out, outn, 0, 0);
        return 0;
    }
    if (id == PERLIN_EVENT_PlayerJoin) {
        const u64 player = r_u64(&p);
        const u32 n = r_u32(&p);
        char text[96] = "Welcome, ";
        u32 at = 9;
        for (u32 i = 0; i < n && at < sizeof text - 2; i++)
            text[at++] = (char)p[i];
        text[at++] = '!';
        say(player, text, at);
        return 0;
    }
    if (id == PERLIN_EVENT_PlayerChat) {
        r_u64(&p);
        p += r_u32(&p); /* name */
        const u32 n = r_u32(&p);
        return contains_creeper((const char*)p, n) ? 1u : 0u;
    }
    if (id == PERLIN_EVENT_Command) {
        p += r_u32(&p); /* name */
        p += r_u32(&p); /* args */
        const u64 player = r_u64(&p);
        unsigned char reply[256];
        const int got = perlin_call(PERLIN_OP_OnlinePlayers, 0, 0, reply, sizeof reply);
        u32 count = 0;
        if (got >= 4) {
            const unsigned char* q = reply;
            count = r_u32(&q);
        }
        char text[] = "Hello! Players online: 0";
        text[sizeof text - 2] = (char)('0' + (count % 10));
        say(player, text, sizeof text - 1);
        return 0;
    }
    return 0;
}
