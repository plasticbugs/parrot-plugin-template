// scalen.c -- see scalen.h.
#include <string.h>

#include "cvscale.h"
#include "scalen.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define SCALEN_HOT __attribute__((section(".time_critical.scalen")))
#else
#define SCALEN_HOT
#endif

int scalen_map(scalen_map_t *map, int n, int m) {
    if (m < 1 || m > SCALEN_MAX || m > n || n > 2 * m) return 0;
    const int p = n - m;
    int src = 0, j = 0;
    for (int k = 0; k < m; k++) {
        // pair j at output (2j + 1) m / 2p: evenly spread, as cvscale34's
        // second of each three for n = 4/3 m
        const int is = j < p && k == (2 * j + 1) * m / (2 * p);
        map->pos[k] = (uint16_t)src;
        map->pair[k] = (uint8_t)is;
        src += is ? 2 : 1;
        j += is;
    }
    map->n = n;
    map->m = m;
    return 1;
}

SCALEN_HOT void scalen_row(const uint8_t *src, int sstride, int w, int h, const scalen_map_t *rows, int r, uint8_t *line) {
    const int y = rows->pos[r];
    const uint8_t *u = src + y * sstride;
    if (!rows->pair[r]) {
        memcpy(line, u, (size_t)w);
        return;
    }
    const uint8_t *v = u + sstride;
    const uint8_t *b = y > 0 ? u - sstride : NULL, *a = y + 2 < h ? v + sstride : NULL;
    for (int x = 0; x < w; x++) {
        const uint8_t before = b ? b[x] : 0, after = a ? a[x] : 0;
        const uint8_t m = cvscale_pick(u[x], v[x], before, after);
        if (cvscale_mid_asks(m, u[x], v[x], before, after)) {
            const uint8_t ub = x ? u[x - 1] : 0, ua = x + 1 < w ? u[x + 1] : 0;
            const uint8_t vb = x ? v[x - 1] : 0, va = x + 1 < w ? v[x + 1] : 0;
            line[x] = cvscale_mid_then(u[x], v[x], ub, ua, vb, va);
        } else {
            line[x] = m;
        }
    }
}

SCALEN_HOT void scalen_line(const uint8_t *line, const scalen_map_t *cols, int k0, int k1, uint8_t *dst) {
    const int w = cols->n;
    for (int k = k0; k < k1; k++) {
        const int x = cols->pos[k];
        if (!cols->pair[k]) {
            dst[k] = line[x];
        } else {
            const uint8_t before = x > 0 ? line[x - 1] : 0, after = x + 2 < w ? line[x + 2] : 0;
            dst[k] = cvscale_pick(line[x], line[x + 1], before, after);
        }
    }
}

void scalen(const uint8_t *src, int sstride, int w, int h, const scalen_map_t *cols, const scalen_map_t *rows,
            int r0, int r1, uint8_t *dst, int dstride) {
    static uint8_t line[SCALEN_LINE_MAX];
    for (int r = r0; r < r1; r++, dst += dstride) {
        scalen_row(src, sstride, w, h, rows, r, line);
        scalen_line(line, cols, 0, cols->m, dst);
    }
}
