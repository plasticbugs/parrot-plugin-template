// lib/scalen's smoke test (the kit's own), run by lib/test.sh
// N = 4k to 3k must be cvscale34's picture exactly; other sizes must keep
// a one-pixel stroke, spread the pairs evenly, and give the same result a
// band at a time as all at once.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cvscale.h"
#include "scalen/scalen.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t seed = 1;
static uint32_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

int main(void) {
    static uint8_t src[256 * 304], a[192 * 256], b[192 * 256];
    static scalen_map_t cols, rows;

    // 1. 224 x 256 to 168 x 192: cvscale34's, on random blocks, strokes and noise
    for (int t = 0; t < 20; t++) {
        for (int i = 0; i < 224 * 256; i++) src[i] = (uint8_t)(t < 10 ? (rnd() % 7 == 0 ? rnd() % 4 : 0) : rnd() % 3);
        cvscale34(src, 224, 224, 256, 0, 224, a, 168);
        CHECK(scalen_map(&cols, 224, 168) && scalen_map(&rows, 256, 192), "map 224/256");
        scalen(src, 224, 224, 256, &cols, &rows, 0, 192, b, 168);
        CHECK(!memcmp(a, b, 168 * 192), "picture %d differs from cvscale34's", t);
    }

    // 2. 278 x 215 to 256 x 179: a one-pixel line in every column and every
    //    line, alone on black, survives as one line
    CHECK(scalen_map(&cols, 278, 256) && scalen_map(&rows, 215, 179), "map 278/215");
    for (int x = 0; x < 278; x++) {
        memset(src, 0, sizeof src);
        for (int y = 0; y < 215; y++) src[y * 278 + x] = 5;
        scalen(src, 278, 278, 215, &cols, &rows, 0, 179, b, 256);
        int lit = 0;
        for (int k = 0; k < 256; k++) lit += b[100 * 256 + k] == 5;
        CHECK(lit == 1, "column %d: %d lit after scaling", x, lit);
    }
    for (int y = 0; y < 215; y++) {
        memset(src, 0, sizeof src);
        memset(src + y * 278, 5, 278);
        scalen(src, 278, 278, 215, &cols, &rows, 0, 179, b, 256);
        int lit = 0;
        for (int r = 0; r < 179; r++) lit += b[r * 256 + 100] == 5;
        CHECK(lit == 1, "line %d: %d lit after scaling", y, lit);
    }

    // 3. the pairs evenly spread: never two outputs in a row both pairs,
    //    and the map uses every source once
    static const int sizes[][2] = {{278, 256}, {215, 179}, {292, 256}, {240, 192}, {256, 256}, {512, 256}, {257, 256}};
    for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        const int n = sizes[s][0], m = sizes[s][1];
        CHECK(scalen_map(&cols, n, m), "map %d/%d", n, m);
        int used = 0, pairs = 0, gap = m, last = -m;
        for (int k = 0; k < m; k++) {
            CHECK(cols.pos[k] == used, "%d/%d: output %d starts at %d, not %d", n, m, k, cols.pos[k], used);
            used += cols.pair[k] ? 2 : 1;
            if (cols.pair[k]) {
                if (pairs) gap = gap < k - last ? gap : k - last;
                last = k;
                pairs++;
            }
        }
        CHECK(used == n && pairs == n - m, "%d/%d: %d used, %d pairs", n, m, used, pairs);
        if (pairs > 1 && 2 * pairs < m) CHECK(gap >= m / pairs - 1, "%d/%d: two pairs %d apart", n, m, gap);
    }
    CHECK(!scalen_map(&cols, 100, 200) && !scalen_map(&cols, 600, 256) && !scalen_map(&cols, 600, 300), "bad sizes refused");

    // 4. a band at a time, and a piece of a line, are the whole's
    CHECK(scalen_map(&cols, 292, 256) && scalen_map(&rows, 240, 192), "map 292/240");
    for (int i = 0; i < 292 * 240; i++) src[i] = (uint8_t)(rnd() % 5 ? 0 : rnd() % 16);
    scalen(src, 292, 292, 240, &cols, &rows, 0, 192, a, 256);
    for (int r0 = 0; r0 < 192; r0 += 8) scalen(src, 292, 292, 240, &cols, &rows, r0, r0 + 8, b + r0 * 256, 256);
    CHECK(!memcmp(a, b, 256 * 192), "by bands differs");
    uint8_t line[292], piece[256];
    scalen_row(src, 292, 292, 240, &rows, 77, line);
    memset(piece, 0xEE, sizeof piece);
    scalen_line(line, &cols, 40, 48, piece);
    CHECK(!memcmp(piece + 40, a + 77 * 256 + 40, 8) && piece[39] == 0xEE && piece[48] == 0xEE, "a piece of a line differs");

    printf(fails ? "lib/scalen: %d FAILED\n" : "lib/scalen: ok\n", fails);
    return fails != 0;
}
