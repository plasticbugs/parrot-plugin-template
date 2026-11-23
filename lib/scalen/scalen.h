// scalen.h -- a picture's pens scaled from N columns to M, and N lines to
// M, keeping strokes one pixel wide, for an arcade picture that doesn't
// scale by 3/4 (Robotron's 278 x 215 play field to 256 x 179, or its
// 292 x 240 to 256 x 192).
//
// It works as the SDK's cvscale34 (sdk/firmware/revc/sdk/cvscale.h) does,
// with the pairs spread evenly instead of one in every four: of N columns,
// N - M pairs become one, the rest are kept. Two that become one: the same
// pen, or the one unlike both its neighbours if one is (a stroke one pixel
// wide survives), or else the first. Lines are merged first, then columns,
// so the result for N = 4k to M = 3k is exactly cvscale34's.
//
//   static scalen_map_t cols, rows;
//   scalen_map(&cols, 278, 256);        // once
//   scalen_map(&rows, 215, 179);
//   scalen(src, 304, 278, 215, &cols, &rows, 0, 179, dst, 256);
//
// A band at a time: scalen(..., r0, r1, band, 256) makes output lines r0
// to r1 - 1 only, into band's first line on, so a plugin can scale one
// 8-line band into a small buffer, encode it and go on to the next
// (Donkey Kong Jr. keeps 24 KB of screen instead of 48 that way). It reads
// the source lines those output lines need and their neighbours.
//
// Pens in, pens out: any 8-bit values, compared only for being the same.
// Outside the source, neighbours are 0. M <= N <= 2 M, M <= SCALEN_MAX,
// source lines up to SCALEN_LINE_MAX wide.
#pragma once
#include <stdint.h>

#define SCALEN_MAX      256
#define SCALEN_LINE_MAX 1024

typedef struct {
    int n, m;
    uint16_t pos[SCALEN_MAX];    // output k's (first) source column or line
    uint8_t pair[SCALEN_MAX];    // 1: it's pos[k] and pos[k] + 1 made one
} scalen_map_t;

// n columns (or lines) to m, the n - m pairs spread evenly. 0 if it can't
// (m > n, n > 2 m, or m > SCALEN_MAX).
int scalen_map(scalen_map_t *map, int n, int m);

// The output lines r0 to r1 - 1 of a w x h picture at src (a line every
// sstride bytes), scaled by cols and rows (cols->n == w, rows->n == h),
// into dst (dst's first line is output line r0, a line every dstride).
void scalen(const uint8_t *src, int sstride, int w, int h, const scalen_map_t *cols, const scalen_map_t *rows,
            int r0, int r1, uint8_t *dst, int dstride);

// The pieces, for a plugin that redraws only what changed:
// output line r before its columns are scaled (w pens: the line, or the
// two lines made one) ...
void scalen_row(const uint8_t *src, int sstride, int w, int h, const scalen_map_t *rows, int r, uint8_t *line);
// ... and output columns k0 to k1 - 1 of a line of cols->n pens, into
// dst[k0] to dst[k1 - 1]. The columns either side are read as neighbours,
// so a piece comes out as the whole line would have it.
void scalen_line(const uint8_t *line, const scalen_map_t *cols, int k0, int k1, uint8_t *dst);
