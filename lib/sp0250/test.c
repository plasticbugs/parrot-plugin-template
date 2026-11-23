// lib/sp0250/test.c -- a smoke test of the SP0250: two made-up frames (a
// voiced one, then noise) and the samples MAME 0.288's SP0250 makes from
// them (as the Star Trek plugin's copy, which matched MAME, made them), the
// DRQ line, and the FIFO. Exits 1 on a failure. Build and run (README.md):
//   cc -std=c11 -O1 -Wall -Wextra -Ilib lib/sp0250/test.c -o /tmp/sp0250test && /tmp/sp0250test
#include <stdio.h>
#include <string.h>

#include "sp0250/sp0250.c"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// (B, F) pairs at 0-1, 3-4, 6-7, 9-10, 11-12, 13-14; 2 amplitude, 5 pitch, 8 repeat | voiced
static const uint8_t f1[15] = {0x30, 0x9c, 0xaa, 0x28, 0x10, 40, 0x20, 0x8c, 0x43, 0x18, 0x08, 0x10, 0x84, 0x08, 0x02};
static const uint8_t f2[15] = {0x28, 0x90, 0x90, 0x20, 0x88, 10, 0x18, 0x04, 0x05, 0x10, 0x82, 0x08, 0x81, 0x04, 0x01};
static const int8_t want_first[24] = {5, 2, -9, -7, 7, 6, -4, -4, 0, 0, 0, 0, -1, -1, -1, 0, 0, 0, 0, -1, -1, -1, -1, -1};
#define WANT_HASH 0xfc64cb99u   // FNV-1a of the 3000 samples
#define WANT_LFSR 0x605d

int main(void) {
    CHECK(SP0250_RATE(3120000) == 10000, "rate %d", SP0250_RATE(3120000));
    sp0250_t s;
    sp0250_init(&s);
    CHECK(sp0250_drq(&s) && s.lfsr == 0x7fff && s.samples == 0, "power on");
    for (int i = 0; i < 15; i++) {
        CHECK(sp0250_drq(&s), "DRQ before byte %d", i);
        sp0250_write(&s, f1[i]);
    }
    CHECK(!sp0250_drq(&s), "DRQ with the FIFO full");
    sp0250_write(&s, 0x55);         // a 16th: dropped
    CHECK(s.fifo_pos == 15 && s.fifo[14] == f1[14], "FIFO overflow");

    uint32_t h = 2166136261u;
    int8_t first[24];
    int f2_at = -1, peak = 0;
    for (int t = 0; t < 3000; t++) {
        if (sp0250_drq(&s) && f2_at < 0) {
            f2_at = t;
            for (int i = 0; i < 15; i++) sp0250_write(&s, f2[i]);
        }
        const int8_t v = sp0250_next(&s);
        if (t < 24) first[t] = v;
        if (v > peak) peak = v;
        h = (h ^ (uint8_t)v) * 16777619u;
    }
    // the reset's frame (all zeros, repeat 0) ends at once: frame 1 is taken
    // at the first sample and DRQ comes back
    CHECK(f2_at == 1, "DRQ again before sample %d", f2_at);
    CHECK(!memcmp(first, want_first, sizeof first), "the first samples: %d %d %d %d ...", first[0], first[1], first[2], first[3]);
    CHECK(h == WANT_HASH && s.lfsr == WANT_LFSR, "hash %08x lfsr %04x", (unsigned)h, s.lfsr);
    CHECK(s.samples == 3000 && peak > 30, "samples %llu peak %d", (unsigned long long)s.samples, peak);

    // with no frame waiting, it carries on as it was, a repeat at a time
    CHECK(sp0250_drq(&s) && s.repeat == 1, "idle: DRQ %d repeat %d", sp0250_drq(&s), s.repeat);

    // render is next n times
    sp0250_t a, b;
    sp0250_init(&a);
    for (int i = 0; i < 15; i++) sp0250_write(&a, f1[i]);
    b = a;
    int8_t buf[500], one[500];
    sp0250_render(&a, buf, 500);
    for (int i = 0; i < 500; i++) one[i] = sp0250_next(&b);
    CHECK(!memcmp(buf, one, sizeof buf) && a.samples == 500, "render");

    if (fails) { printf("sp0250: %d failed\n", fails); return 1; }
    printf("sp0250: all passed\n");
    return 0;
}
