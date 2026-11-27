// SPDX-License-Identifier: LGPL-2.1
// lib/nescv/test.c -- the smoke test: pictures through the encoder, its
// lists run on a model of the console's video RAM (the frame kernel's list
// format, sdk/gbcv/kernel/gbkernel.asm), and the screen read back from it
// as the TMS9918 shows Graphics II, pixel for pixel.
//
//   1. a whole picture, a band at a time through a colour table
//      (nescv_band), every 8-pixel row in two colours: it must come back
//      exactly, within a second of TV frames, each list inside its size
//   2. part of a picture: some columns of some bands, pixels that are
//      colours already (nescv_band_cols with no table): those cells change,
//      the rest keep the first picture's
//   3. a row in three colours: it loses one, and the clash is counted
// and through it all the sound bytes reach the PSG once a frame, and the
// pattern index matches the slots. Built twice by lib/test.sh, with and
// without NESCV_FAST_KEYS. Prints "lib/nescv: ok" and exits 0.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nescv/nescv.h"

static nescv_t enc;                     // (big: the memo and the index)
static uint8_t list[NESCV_LIST_MAX];
static uint8_t vram[0x4000], reg[8];
static uint8_t psg_got[64];
static int psg_n, fails;

#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("lib/nescv: " __VA_ARGS__); printf("\n"); } } while (0)

// one list, as the kernel runs it: runs, PSG bytes and registers in the
// vertical blank part, runs and PSG bytes after it, 0 ending each part
static void run_list(const uint8_t *l, size_t n) {
    size_t i = 0;
    int part = 0;                       // 0 the vertical blank part, 1 the rest
    psg_n = 0;
    while (i < n) {
        const uint8_t c = l[i++];
        if (c == 0) {
            if (++part == 2) break;
        } else if (c == 1) {
            const uint16_t at = (uint16_t)(l[i] | (l[i + 1] & 0x3F) << 8);
            CHECK(l[i + 1] & 0x40, "a run's address without the write bit");
            const int k = l[i + 2];
            CHECK(k >= 1 && at + k <= 0x4000, "a run of %d at %04X", k, at);
            memcpy(vram + at, l + i + 3, (size_t)k);
            i += 3 + (size_t)k;
        } else if (c == 2) {
            const int k = l[i];
            memcpy(psg_got + psg_n, l + i + 1, (size_t)k);
            psg_n += k;
            i += 1 + (size_t)k;
        } else if (c == 3 && part == 0) {
            CHECK(l[i + 1] & 0x80, "a register write without its bit");
            reg[l[i + 1] & 7] = l[i];
            i += 2;
        } else {
            CHECK(0, "command %d in part %d", c, part);
            return;
        }
    }
    CHECK(part == 2 && i == n, "the list doesn't end where it says (%zu of %zu)", i, n);
}

// the screen's pixel at (x, y), as the VDP shows it: names at R2, each
// third's patterns at $0000 and colours at $2000, colour 0 the border (R7)
static int pixel(int x, int y) {
    const int r = y >> 3, c = x >> 3, third = r >> 3;
    const uint8_t name = vram[reg[2] * 0x400 + r * 32 + c];
    const int at = third * 0x800 + name * 8 + (y & 7);
    const uint8_t pat = vram[at], col = vram[0x2000 + at];
    const int v = (pat >> (7 - (x & 7)) & 1) ? col >> 4 : col & 15;
    return v ? v : reg[7] & 15;
}

// TV frames until the picture is on screen (or a second's worth): each list
// run, its PSG bytes checked
static int frames_until(uint64_t index) {
    static const uint8_t psg[11] = {0x80, 0, 0x9F, 0xA0, 0, 0xBF, 0xC0, 0, 0xDF, 0xE0, 0xFF};
    for (int f = 1; f <= 60; f++) {
        const size_t n = nescv_tv_frame(&enc, psg, 11, list);
        CHECK(n > 0 && n <= NESCV_LIST_MAX, "a list of %zu bytes", n);
        run_list(list, n);
        CHECK(psg_n == 11 && !memcmp(psg_got, psg, 11), "frame %d: %d PSG bytes, not the 11 sent", f, psg_n);
        if (enc.shown_index == index) return f;
    }
    return -1;
}

static uint8_t want[192][256];          // the colour each pixel should show

static int differences(void) {
    int bad = 0;
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 256; x++)
            if (pixel(x, y) != want[y][x] && bad++ < 3) printf("  (%d, %d): %d, not %d\n", x, y, pixel(x, y), want[y][x]);
    return bad;
}

int main(void) {
    reg[2] = 6;                         // the kernel's start: names at $1800
    nescv_init(&enc);

    // 1. a picture of indices through a table: index i is colour i & 15;
    // each 8-pixel row in two colours (a and b), its bits from the place.
    // Columns repeat every 24, so a third has 192 different cells: each
    // third has 255 slots free (one is on screen), and a picture with more
    // different cells than that holds some back for a picture (nescv.h)
    static uint8_t pic[192][256], lut[256];
    for (int i = 0; i < 256; i++) lut[i] = (uint8_t)((i & 15) ? (i & 15) : 1);
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 256; x++) {
            const int r = y >> 3, c = (x >> 3) % 24;
            const int a = 1 + (r * 7 + c * 3 + (y & 7)) % 15, b = 1 + (a + 4) % 15;
            const int bits = (c * 37 + r * 11 + (y & 7) * 5) & 0xFF;
            const int v = (bits >> (7 - (x & 7)) & 1) ? a : b;
            want[y][x] = (uint8_t)v;
            pic[y][x] = (uint8_t)(v | ((x + y) & 7) << 4);     // (the high bits for the table to drop)
        }
    CHECK(nescv_wants_picture(&enc), "no picture wanted at the start");
    for (int r = 0; r < NESCV_ROWS; r++) nescv_band(&enc, r, &pic[r * 8][0], 256, lut);
    nescv_band_done(&enc, 1, 4);
    int f = frames_until(1);
    CHECK(f > 0, "picture 1 wasn't on screen after 60 TV frames");
    CHECK(differences() == 0, "picture 1 isn't what was sent");
    CHECK(enc.stats.held == 0, "%u cells held back", enc.stats.held);
    CHECK((reg[7] & 15) == 4, "the border is %d, not 4", reg[7] & 15);
    const int f1 = f;

    // 2. columns 4 to 11 of bands 5 to 9 only, from pixels that are colours
    // 1-15 already (no table: the fast key)
    static uint8_t part[192][256];
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 256; x++) {
            const int a = 1 + ((x >> 3) + 2 * (y >> 3)) % 15, b = 1 + (a + 7) % 15;
            part[y][x] = (uint8_t)(((x ^ y) & 1) ? a : b);
        }
    CHECK(nescv_wants_picture(&enc), "no picture wanted after picture 1");
    nescv_band_start(&enc);
    for (int r = 5; r <= 9; r++) nescv_band_cols(&enc, r, &part[r * 8][0], 256, NULL, 4, 12);
    nescv_band_done(&enc, 2, 4);
    for (int y = 40; y < 80; y++)
        for (int x = 32; x < 96; x++) want[y][x] = part[y][x];
    f = frames_until(2);
    CHECK(f > 0, "picture 2 wasn't on screen after 60 TV frames");
    CHECK(differences() == 0, "picture 2 isn't picture 1 with the new cells");

    // 3. one row of a cell in three colours: one of them goes, and it's counted
    const uint32_t clash_before = enc.stats.clash_rows;
    static const uint8_t three[8] = {2, 2, 2, 6, 6, 6, 6, 11};
    memcpy(&part[0][0], three, 8);
    CHECK(nescv_wants_picture(&enc), "no picture wanted after picture 2");
    nescv_band_start(&enc);
    nescv_band_cols(&enc, 0, &part[0][0], 256, NULL, 0, 1);
    nescv_band_done(&enc, 3, 4);
    f = frames_until(3);
    CHECK(f > 0, "picture 3 wasn't on screen after 60 TV frames");
    CHECK(enc.stats.clash_rows == clash_before + 1, "%u rows lost a colour, not 1", enc.stats.clash_rows - clash_before);
    int kept[16] = {0};
    for (int x = 0; x < 8; x++) kept[pixel(x, 0)]++;
    // (11 is nearer 6 than 2, so it shows as 6)
    CHECK(kept[2] == 3 && kept[6] == 5 && kept[11] == 0, "the row shows %d, %d and %d of 2, 6 and 11", kept[2], kept[6], kept[11]);

    CHECK(nescv_check_index(&enc) == 0, "%d index entries point at the wrong slot", nescv_check_index(&enc));
    if (fails) {
        printf("lib/nescv: %d failed\n", fails);
        return 1;
    }
    printf("lib/nescv: ok (picture 1 in %d TV frames, %u uploads, %u memo hits)%s\n", f1, enc.stats.uploads,
           enc.stats.memo_hits,
#ifdef NESCV_FAST_KEYS
           " with NESCV_FAST_KEYS"
#else
           ""
#endif
    );
    return 0;
}
