// SPDX-License-Identifier: LGPL-2.1
// lib/nescv: from the Parrot Cart NES plugin
// (github.com/plasticbugs/parrot-plugin-nes, dfd06f5), under its license,
// the GNU LGPL 2.1 (LICENSE here). The Neo Geo Pocket Color plugin added
// nescv_band_cols() and nescv_band_start() (a band of some columns only),
// NESCV_FAST_KEYS (keys compared a word at a time) and the key for pixels
// that are colours already (lut NULL). README.md says how to use it.
//
// nescv.c -- see nescv.h. The colour choice, the pattern cache index and the
// list helpers are gbcv.c's (the Game Boy plugin's encoder, in
// parrot-plugin-gameboy), copied so that gbcv stays byte for byte its
// model's port; the rest is the NES's.
#include "nescv.h"

#include <string.h>

// ---- colours -------------------------------------------------------------------

// TMS9918 colours as cvengine::video::PALETTE has them (0 = transparent)
static const uint8_t RGB[16][3] = {
    {0, 0, 0},       {0, 0, 0},       {33, 200, 66},   {94, 220, 120},
    {84, 85, 237},   {125, 118, 252}, {212, 82, 77},   {66, 235, 245},
    {252, 85, 84},   {255, 121, 120}, {212, 193, 84},  {230, 206, 128},
    {33, 176, 59},   {201, 91, 186},  {204, 204, 204}, {255, 255, 255},
};

static uint32_t DIST[16][16];   // luma-weighted: 3 dr^2 + 6 dg^2 + db^2

static void dist_init(void) {
    static bool done;
    if (done) return;
    for (int a = 0; a < 16; a++)
        for (int b = 0; b < 16; b++) {
            int32_t r = RGB[a][0] - RGB[b][0], g = RGB[a][1] - RGB[b][1], u = RGB[a][2] - RGB[b][2];
            DIST[a][b] = (uint32_t)(3 * r * r + 6 * g * g + u * u);
        }
    done = true;
}

// The NES's 64 colours as TMS9918 colours, chosen by eye from QuickNES's
// palette (nes_ntsc's default) against OKLab's nearest: a grey stays a grey
// (the nearest to $00 is dark red), the sky ($21) is cyan rather than the
// lavender of colour 5, and the pale colours ($31-$3C) are grey or light
// yellow rather than saturated.
const uint8_t nescv_nes_colours[64] = {
    14, 4, 4, 4, 13, 6, 6, 6, 1, 12, 12, 12, 4, 1, 1, 1,      // $00 dark
    14, 4, 4, 4, 13, 13, 6, 6, 12, 12, 12, 12, 7, 1, 1, 1,    // $10
    15, 7, 5, 13, 13, 13, 9, 10, 10, 3, 3, 3, 7, 1, 1, 1,     // $20
    15, 14, 14, 14, 14, 14, 11, 11, 11, 11, 14, 14, 14, 14, 1, 1, // $30 pale
};

// gbcv.c's pair(): the two colours for a row of nu colours ec[] covering the
// pixels in em[] (in the order the row meets them); 1 if it had to drop one
NESCV_HOT static inline int pair(const uint8_t *ec, const uint8_t *em, int nu, uint8_t *pat, uint8_t *colour) {
    uint8_t fg, bg;
    int clash = 0;
    if (nu == 1) {
        fg = bg = ec[0];
    } else if (nu == 2) {
        fg = ec[0] > ec[1] ? ec[0] : ec[1];
        bg = ec[0] > ec[1] ? ec[1] : ec[0];
    } else {
        uint32_t cnt[8], best = UINT32_MAX;
        for (int k = 0; k < nu; k++) cnt[k] = (uint32_t)__builtin_popcount(em[k]);
        fg = bg = 0;
        for (int i = 0; i < nu; i++)
            for (int j = i + 1; j < nu; j++) {
                uint8_t a = ec[i], b = ec[j];
                uint32_t e = 0;
                for (int k = 0; k < nu; k++) {
                    uint32_t da = DIST[ec[k]][a], db = DIST[ec[k]][b];
                    e += cnt[k] * (da < db ? da : db);
                }
                if (e < best) {
                    best = e;
                    fg = a > b ? a : b;
                    bg = a > b ? b : a;
                }
            }
        clash = 1;
    }
    uint8_t p = 0;
    if (fg != bg)
        for (int k = 0; k < nu; k++)
            if (DIST[ec[k]][fg] < DIST[ec[k]][bg]) p |= em[k];
    *pat = p;
    *colour = (uint8_t)(fg << 4 | bg);
    return clash;
}

// gbcv.c's row_cols(): a row of eight colours
NESCV_HOT static int row_cols(const uint8_t *px, uint8_t *pat, uint8_t *colour) {
    uint8_t ec[8], em[8];
    int nu = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t c = px[i];
        int k = 0;
        while (k < nu && ec[k] != c) k++;
        if (k == nu) { ec[nu] = c; em[nu] = 0; nu++; }
        em[k] |= (uint8_t)(0x80 >> i);
    }
    return pair(ec, em, nu, pat, colour);
}

// ---- the bands -----------------------------------------------------------------

void nescv_bands(const uint8_t *log, int crop, int quantum, nescv_picture_t *p) {
    if (crop < 0) crop = 0;
    if (crop > 48) crop = 48;
    if (quantum != 2 && quantum != 4 && quantum != 8) quantum = 1;
    const int q = quantum - 1;
    int prev = -8;
    for (int b = 0; b < NESCV_ROWS; b++) {
        int n = crop + 8 * b, s = -1;
        // the nearest line whose fine Y is a multiple of the quantum: n,
        // n+1, n-1 ... (with 8, n-3 and n+4 last: a tile row's start)
        for (int d = 0; d < quantum && s < 0; d++) {
            int c = n + ((d & 1) ? (d + 1) / 2 : -(d / 2));
            if (c >= 0 && c <= 232 && c >= prev + 8 && (quantum == 1 || (log[c * 2 + 1] != 0xFF && !(log[c * 2 + 1] & q))))
                s = c;
        }
        if (s < 0) s = n < prev + 8 ? prev + 8 : n;
        if (s > 232) s = 232;
        p->line[b] = (uint8_t)s;
        prev = s;
        // across only when the band's lines share their fine X: one that
        // changes line by line (a road's curve, a raster floor) is shown
        // as it is
        int fx = 0;
        if (q && log[s * 2] != 0xFF) {
            fx = log[s * 2] & q;
            for (int y = 1; y < 8; y++)
                if (log[(s + y) * 2] != log[s * 2]) fx = 0;
        }
        p->col[b] = (uint16_t)(8 - fx);
    }
}

// ---- the pattern cache index (gbcv.c's) ------------------------------------------

// two cells' 16 bytes compared: memcmp, which is what every plugin that has
// run on the console is built with; four words where NESCV_BESIDE is
// defined (the PC Engine plugin, whose pictures are new in most cells:
// newlib's memcmp goes a byte at a time, 2 ms of a picture's start)
#if defined(NESCV_BESIDE) || defined(NESCV_FAST_KEYS)
static inline uint32_t key_differs(const uint8_t *a, const uint8_t *b) {
    uint32_t x[4], y[4];
    memcpy(x, a, 16);
    memcpy(y, b, 16);
    return (x[0] ^ y[0]) | (x[1] ^ y[1]) | (x[2] ^ y[2]) | (x[3] ^ y[3]);
}
#define KEYCMP(a, b) key_differs(a, b)
#else
#define KEYCMP(a, b) memcmp(a, b, 16)
#endif

NESCV_HOT static uint32_t hash(const uint8_t *k) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 16; i++) h = (h ^ k[i]) * 16777619u;
    return h;
}

// at most 256 entries in 512, so a probe always meets an empty entry (and
// the probes are bounded all the same)
NESCV_HOT static int idx_get(nescv_t *e, int t, const uint8_t *k) {
    nescv_ent_t *tab = e->index[t];
    uint32_t i = hash(k) & (NESCV_HASH - 1);
    for (int n = 0; n < NESCV_HASH; n++, i = (i + 1) & (NESCV_HASH - 1)) {
        if (!tab[i].used) return -1;
        if (!KEYCMP(tab[i].key, k)) return tab[i].slot;
    }
    return -1;
}

NESCV_HOT static void idx_put(nescv_t *e, int t, const uint8_t *k, uint8_t slot) {
    nescv_ent_t *tab = e->index[t];
    uint32_t i = hash(k) & (NESCV_HASH - 1);
    int n = 0;
    while (tab[i].used && KEYCMP(tab[i].key, k)) {
        i = (i + 1) & (NESCV_HASH - 1);
        if (++n == NESCV_HASH) {           // full (it can't be): start the index again
            memset(tab, 0, sizeof e->index[t]);
            e->stats.index_resets++;
            idx_put(e, t, k, slot);
            return;
        }
    }
    memcpy(tab[i].key, k, 16);
    tab[i].slot = slot;
    tab[i].used = true;
}

// linear probing, deletion by shifting later entries back
NESCV_HOT static void idx_del(nescv_t *e, int t, const uint8_t *k) {
    nescv_ent_t *tab = e->index[t];
    uint32_t i = hash(k) & (NESCV_HASH - 1);
    for (int n = 0; tab[i].used && KEYCMP(tab[i].key, k); n++) {
        if (n == NESCV_HASH) return;
        i = (i + 1) & (NESCV_HASH - 1);
    }
    if (!tab[i].used) return;
    for (uint32_t j = i;;) {
        tab[i].used = false;
        for (;;) {
            j = (j + 1) & (NESCV_HASH - 1);
            if (!tab[j].used) return;
            uint32_t h = hash(tab[j].key) & (NESCV_HASH - 1);
            if (i <= j ? (i < h && h <= j) : (i < h || h <= j)) continue;
            break;
        }
        tab[i] = tab[j];
        i = j;
    }
}

int nescv_check_index(const nescv_t *e) {
    int bad = 0;
    for (int t = 0; t < 3; t++)
        for (int i = 0; i < NESCV_HASH; i++) {
            const nescv_ent_t *x = &e->index[t][i];
            if (x->used && KEYCMP(e->pat[t][x->slot], x->key)) bad++;
        }
    return bad;
}

// ---- lists (gbcv.c's, from gbkernel.asm's costs) ---------------------------------

#define FRAME_T    (262 * 228)
#define VBLANK_T   (70 * 228)
#define C_START    480
#define C_RUN_VB   106
#define C_BYTE_VB  23
#define C_RUN_ACT  106
#define C_BYTE_ACT 31
#define C_PSG      75
#define C_PSG_BYTE 52
#define C_REG      114
#define C_END_VB   54
#define C_END      52
#define VB_LIMIT   (VBLANK_T - 600)
#define LIST_LIMIT (FRAME_T - 1200)

typedef struct {
    uint8_t *b;
    size_t n;
    uint32_t t;
    bool vblank;
} list_t;

static bool fits(const list_t *l, uint32_t runs, uint32_t bytes) {
    if (l->vblank) return l->t + runs * C_RUN_VB + bytes * C_BYTE_VB + C_END_VB <= VB_LIMIT;
    return l->t + runs * C_RUN_ACT + bytes * C_BYTE_ACT + C_END <= LIST_LIMIT;
}

static bool fits_active(const list_t *l, uint32_t runs, uint32_t bytes) {
    uint32_t end_vb = l->vblank ? C_END_VB : 0;
    return l->t + end_vb + runs * C_RUN_ACT + bytes * C_BYTE_ACT + C_END <= LIST_LIMIT;
}

static void l_run(list_t *l, uint16_t addr, const uint8_t *d, int n) {
    l->t += l->vblank ? C_RUN_VB + n * C_BYTE_VB : C_RUN_ACT + n * C_BYTE_ACT;
    l->b[l->n++] = 1;
    l->b[l->n++] = (uint8_t)addr;
    l->b[l->n++] = (uint8_t)((addr >> 8) & 0x3F) | 0x40;
    l->b[l->n++] = (uint8_t)n;
    memcpy(l->b + l->n, d, n);
    l->n += n;
}

static void l_psg(list_t *l, const uint8_t *d, int n) {
    while (n > 0) {
        int k = n > 255 ? 255 : n;
        l->t += C_PSG + C_PSG_BYTE * k;
        l->b[l->n++] = 2;
        l->b[l->n++] = (uint8_t)k;
        memcpy(l->b + l->n, d, k);
        l->n += k;
        d += k;
        n -= k;
    }
}

static void l_reg(list_t *l, uint8_t r, uint8_t v) {
    l->t += C_REG;
    l->b[l->n++] = 3;
    l->b[l->n++] = v;
    l->b[l->n++] = 0x80 | r;
}

static void l_end_vblank(list_t *l) {
    if (!l->vblank) return;
    l->vblank = false;
    l->t += C_END_VB;
    l->b[l->n++] = 0;
}

// ---- the console -------------------------------------------------------------

#define PATTERNS 0x0000
#define COLOURS  0x2000
static const uint16_t NAMES[2] = {0x1800, 0x1C00};   // R2 = 6, 7

void nescv_init(nescv_t *e) {
    dist_init();
    memset(e, 0, sizeof *e);
    e->vram_r7 = -1;
    e->backdrop = 1;
    e->now = 1;
    e->refresh_pass = 1;
}

bool nescv_wants_picture(const nescv_t *e) {
    if (!e->has_target) return true;
    return e->up_head == e->up_tail &&
           !memcmp(e->target_names, e->vram_names[!e->front], NESCV_CELLS);
}

// (Parrot Cart NGPC) 8 pixels that are colours 1-15 already, as the key:
// the first in the top 4 bits
static inline uint32_t key8(const uint8_t *px) {
    uint32_t a, b;
    memcpy(&a, px, 4);
    memcpy(&b, px + 4, 4);
    a = __builtin_bswap32(a);
    b = __builtin_bswap32(b);
    a |= a >> 4;                             // pixel pairs in bytes 2 and 0
    b |= b >> 4;
    return (a >> 16 & 0xFF) << 24 | (a & 0xFF) << 16 | (b >> 16 & 0xFF) << 8 | (b & 0xFF);
}

// band r's cells from its 8 lines (256 pixels each, pitch apart, through
// lut; no lut: the pixels are colours 1-15 already)
NESCV_HOT static void band_cells(nescv_t *e, int r, const uint8_t *band, long pitch, const uint8_t *lut, int c0, int c1) {
    uint32_t clash = 0, hits = 0, misses = 0;
    for (int c = c0; c < c1; c++) {
        uint8_t *cell = e->latest[r * NESCV_COLS + c];
        const uint8_t *px = band + c * 8;
        for (int y = 0; y < 8; y++, px += pitch) {
            uint8_t col[8];
            uint32_t key = 0;
            if (!lut) {
                key = key8(px);
                nescv_memo_t *m = &e->memo[(key * 2654435761u) >> (32 - NESCV_MEMO_BITS)];
                if (m->key == key) {
                    hits++;
                } else {
                    misses++;
                    for (int x = 0; x < 8; x++) col[x] = (uint8_t)(key >> (28 - 4 * x) & 15);
                    m->key = key;
                    m->clash = (uint8_t)row_cols(col, &m->pat, &m->colour);
                }
                cell[y] = m->pat;
                cell[8 + y] = m->colour;
                clash += m->clash;
                continue;
            }
            for (int x = 0; x < 8; x++) {
                col[x] = lut[px[x]];
                key = key << 4 | col[x];
            }
            nescv_memo_t *m = &e->memo[(key * 2654435761u) >> (32 - NESCV_MEMO_BITS)];
            if (m->key == key) {
                hits++;
            } else {
                misses++;
                m->key = key;
                m->clash = (uint8_t)row_cols(col, &m->pat, &m->colour);
            }
            cell[y] = m->pat;
            cell[8 + y] = m->colour;
            clash += m->clash;
        }
    }
    e->stats.memo_hits += hits;
    e->stats.memo_misses += misses;
    e->band_clash += clash;
}

#ifdef NESCV_LUTS
// band_cells() with four tables: the pixel at (x, y) of the screen through
// luts[(y & 1) * 2 + (x & 1)] (nescv_band_luts). Its own copy, and built
// only where NESCV_LUTS is defined (the Master System plugin): a second
// caller of row_cols() changes what the compiler makes of band_cells(), and
// the other plugins' is the one that has run on the console.
NESCV_HOT static void band_cells_luts(nescv_t *e, int r, const uint8_t *band, long pitch, const uint8_t *const *luts) {
    uint32_t clash = 0, hits = 0, misses = 0;
    for (int c = 0; c < NESCV_COLS; c++) {
        uint8_t *cell = e->latest[r * NESCV_COLS + c];
        const uint8_t *px = band + c * 8;
        for (int y = 0; y < 8; y++, px += pitch) {
            const uint8_t *const l0 = luts[(y & 1) * 2], *const l1 = luts[(y & 1) * 2 + 1];
            uint8_t col[8];
            uint32_t key = 0;
            for (int x = 0; x < 8; x += 2) {
                col[x] = l0[px[x]];
                col[x + 1] = l1[px[x + 1]];
                key = key << 8 | (uint32_t)(col[x] << 4 | col[x + 1]);
            }
            nescv_memo_t *m = &e->memo[(key * 2654435761u) >> (32 - NESCV_MEMO_BITS)];
            if (m->key == key) {
                hits++;
            } else {
                misses++;
                m->key = key;
                m->clash = (uint8_t)row_cols(col, &m->pat, &m->colour);
            }
            cell[y] = m->pat;
            cell[8 + y] = m->colour;
            clash += m->clash;
        }
    }
    e->stats.memo_hits += hits;
    e->stats.memo_misses += misses;
    e->band_clash += clash;
}

#endif

static void picture_done(nescv_t *e, uint64_t index, uint8_t backdrop) {
    e->stats.offered++;
    e->latest_clash = e->band_clash;
    e->band_clash = 0;
    e->latest_index = index;
    e->latest_backdrop = backdrop ? backdrop : 1;
    e->has_latest = true;
}

NESCV_HOT bool nescv_offer(nescv_t *e, const nescv_picture_t *p, uint64_t index) {
    if (e->has_target && e->up_head != e->up_tail) return false;
    e->band_clash = 0;
    for (int r = 0; r < NESCV_ROWS; r++)
        band_cells(e, r, p->pixels + (long)p->line[r] * p->pitch + p->col[r], p->pitch, p->lut, 0, NESCV_COLS);
    picture_done(e, index, p->backdrop);
    return true;
}

NESCV_HOT void nescv_band(nescv_t *e, int r, const uint8_t *px, long pitch, const uint8_t *lut) {
    if (r == 0) e->band_clash = 0;
    if (r >= 0 && r < NESCV_ROWS) band_cells(e, r, px, pitch, lut, 0, NESCV_COLS);
}

NESCV_HOT void nescv_band_cols(nescv_t *e, int r, const uint8_t *px, long pitch, const uint8_t *lut, int c0, int c1) {
    if (r >= 0 && r < NESCV_ROWS && c0 >= 0 && c1 <= NESCV_COLS) band_cells(e, r, px, pitch, lut, c0, c1);
}

void nescv_band_start(nescv_t *e) { e->band_clash = 0; }

#ifdef NESCV_LUTS
NESCV_HOT void nescv_band_luts(nescv_t *e, int r, const uint8_t *px, long pitch, const uint8_t *const luts[4]) {
    if (r == 0) e->band_clash = 0;
    if (r >= 0 && r < NESCV_ROWS) band_cells_luts(e, r, px, pitch, luts);
}
#endif

void nescv_band_done(nescv_t *e, uint64_t index, uint8_t backdrop) { picture_done(e, index, backdrop); }

#ifdef NESCV_BESIDE
// band_cells() into the caller's cells, for a picture made over several TV
// frames beside the encoder (the PC Engine plugin: nescv_cells(),
// nescv_offer_cells()). Its own copy, built only where NESCV_BESIDE is
// defined, for the reason band_cells_luts() is.
NESCV_HOT uint32_t nescv_cells(nescv_t *e, const uint8_t *band, long pitch, const uint8_t *lut, uint8_t (*cells)[16]) {
    uint32_t clash = 0, hits = 0, misses = 0;
    for (int c = 0; c < NESCV_COLS; c++) {
        uint8_t *cell = cells[c];
        const uint8_t *px = band + c * 8;
        for (int y = 0; y < 8; y++, px += pitch) {
            uint8_t col[8];
            uint32_t key = 0;
            for (int x = 0; x < 8; x++) {
                col[x] = lut[px[x]];
                key = key << 4 | col[x];
            }
            nescv_memo_t *m = &e->memo[(key * 2654435761u) >> (32 - NESCV_MEMO_BITS)];
            if (m->key == key) {
                hits++;
            } else {
                misses++;
                m->key = key;
                m->clash = (uint8_t)row_cols(col, &m->pat, &m->colour);
            }
            cell[y] = m->pat;
            cell[8 + y] = m->colour;
            clash += m->clash;
        }
    }
    e->stats.memo_hits += hits;
    e->stats.memo_misses += misses;
    return clash;
}

bool nescv_takes_cells(const nescv_t *e) {
    return !e->has_latest && !(e->has_target && e->up_head != e->up_tail);
}

bool nescv_offer_cells(nescv_t *e, const uint8_t (*cells)[16], uint32_t clash, uint64_t index, uint8_t backdrop) {
    if (!nescv_takes_cells(e)) return false;
    memcpy(e->latest, cells, sizeof e->latest);
    e->band_clash = clash;
    picture_done(e, index, backdrop);
    return true;
}
#endif

#ifndef NESCV_020
// how far a cell's new look is from what it shows now: pattern bits that
// differ, and 8 for each row whose colours differ (0-128)
NESCV_HOT static uint8_t look_change(const uint8_t *a, const uint8_t *b) {
    uint32_t p[2], q[2];
    memcpy(p, a, 8);
    memcpy(q, b, 8);
    int d = __builtin_popcount(p[0] ^ q[0]) + __builtin_popcount(p[1] ^ q[1]);
    for (int y = 8; y < 16; y++) d += a[y] != b[y] ? 8 : 0;
    return (uint8_t)d;
}
#endif

#ifdef NESCV_020
// start() as the NES plugin's 0.2.0 had it (5f32104), the last one seen
// right on the console: a third out of free slots overwrites the least
// recently shown slot on screen, and nothing is rewritten (no refresh()).
// NES 0.4.0 was built so; nes/plugin's -DNES_NESCV_020=ON builds it again,
// to compare with on the console.
NESCV_HOT static void start(nescv_t *e) {
    // slots on screen now must not change until the flip
    static bool busy[3][256], mine[3][256];
    memset(busy, 0, sizeof busy);
    memset(mine, 0, sizeof mine);
    const uint8_t *front = e->vram_names[e->front];
    for (int i = 0; i < NESCV_CELLS; i++) busy[i >> 8][front[i]] = true;
    e->up_head = e->up_tail = 0;
    for (int i = 0; i < NESCV_CELLS; i++) {
        int t = i >> 8;                        // 8 rows of 32: 256 cells a third
        const uint8_t *cell = e->latest[i];
        int s = idx_get(e, t, cell);
        if (s >= 0) {
            e->target_names[i] = (uint8_t)s;
            mine[t][s] = true;
            e->stats.hits++;
            continue;
        }
        // least recently shown slot that is neither on screen nor taken
        int pick = -1;
        for (int k = 0; k < 256; k++)
            if (!busy[t][k] && !mine[t][k] && (pick < 0 || e->age[t][k] < e->age[t][pick])) pick = k;
        if (pick < 0) {
            // out of free slots: overwrite one on screen (wrong until the
            // flip), never one this picture has taken already; this third
            // has had fewer than 256 cells so far, so there is one
            e->stats.overflow++;
            for (int k = 0; k < 256; k++)
                if (!mine[t][k] && (pick < 0 || e->age[t][k] < e->age[t][pick])) pick = k;
        }
        const uint8_t *old = e->pat[t][pick];
        if (idx_get(e, t, old) == pick) idx_del(e, t, old);
        idx_put(e, t, cell, (uint8_t)pick);
        mine[t][pick] = true;
        e->target_names[i] = (uint8_t)pick;
        nescv_upload_t *u = &e->uploads[e->up_tail++];
        u->cell = (uint16_t)i;
        u->third = (uint8_t)t;
        u->slot = (uint8_t)pick;
    }
    e->target_index = e->latest_index;
    e->target_clash = e->latest_clash;
    e->target_backdrop = e->latest_backdrop;
    e->has_target = true;
    if (e->up_head == e->up_tail && e->target_backdrop == e->backdrop &&
        !memcmp(e->target_names, front, NESCV_CELLS)) {
        // the picture on screen already: nothing to write
        e->has_target = false;
        e->shown_index = e->target_index;
        e->stats.same++;
    }
}
#else
NESCV_HOT static void start(nescv_t *e) {
    // slots on screen now must not change until the flip (busy), and
    // the ones this picture shows can't take another cell (mine)
    static bool busy[3][256], mine[3][256], mixed[3][256], miss[NESCV_CELLS];
    static int16_t first[3][256];      // a cell on screen showing each slot (-1: none)
    static uint16_t order[NESCV_CELLS], bucket[130];
    static uint8_t change[NESCV_CELLS];
    memset(busy, 0, sizeof busy);
    memset(mine, 0, sizeof mine);
    memset(mixed, 0, sizeof mixed);
    memset(first, 0xFF, sizeof first);
    const uint8_t *front = e->vram_names[e->front];
    // a slot is mixed when the cells showing it now want different new looks:
    // none of them can rewrite it in place
    for (int i = 0; i < NESCV_CELLS; i++) {
        const int t = i >> 8, s = front[i];
        busy[t][s] = true;
        if (first[t][s] < 0) first[t][s] = (int16_t)i;
        else if (!mixed[t][s] && KEYCMP(e->latest[i], e->latest[first[t][s]])) mixed[t][s] = true;
    }
    e->up_head = e->up_tail = 0;
    // the cells in a slot already
    int misses = 0;
    memset(bucket, 0, sizeof bucket);
    for (int i = 0; i < NESCV_CELLS; i++) {
        int t = i >> 8;                        // 8 rows of 32: 256 cells a third
        int s = idx_get(e, t, e->latest[i]);
        miss[i] = s < 0;
        if (s < 0) {
            misses++;
            change[i] = look_change(e->latest[i], e->pat[t][front[i]]);
            bucket[change[i]]++;
            continue;
        }
        e->target_names[i] = (uint8_t)s;
        mine[t][s] = true;
        e->stats.hits++;
    }
    // the misses, those that change most first (they get the free slots
    // when a third runs short)
    for (int d = 128, at = 0; d >= 0; d--) {
        const uint16_t n = bucket[d];
        bucket[d] = (uint16_t)at;
        at += n;
    }
    for (int i = 0; i < NESCV_CELLS; i++)
        if (miss[i]) order[bucket[change[i]]++] = (uint16_t)i;
    // The new ones: a slot not on screen (least recently shown) while there
    // is one. A slot on screen never takes a look its cells don't all want:
    // they would show it until the flip (garbage). So when a third runs out
    // (a screen of text, a busy scroll: a cell for every place), a cell
    // takes its own slot when every cell showing it wants the same new look
    // (they show it early), or else keeps its old look for this picture
    // (held: the next picture has the slots this one frees). Cells that
    // can't take their own go first.
#if defined(NESCV_BESIDE) && !defined(NESCV_PICK_SCAN)   // (NESCV_PICK_SCAN: a test's, to compare the two)
    // each third's slots not on screen, least recently shown first (a slot
    // a cell takes meanwhile is passed over when its turn comes)
    static uint8_t free_list[3][256];
    int free_n[3] = {0, 0, 0}, free_at[3] = {0, 0, 0};
    for (int t = 0; t < 3 && misses; t++)
        for (int k = 0; k < 256; k++) {
            if (busy[t][k]) continue;
            const uint32_t age = e->age[t][k];
            int at = free_n[t]++;
            while (at > 0 && e->age[t][free_list[t][at - 1]] > age) { free_list[t][at] = free_list[t][at - 1]; at--; }
            free_list[t][at] = (uint8_t)k;
        }
#endif
    const int nmiss = misses;
    for (int pass = 0; pass < 2 && misses; pass++)
        for (int m = 0; m < nmiss; m++) {
            const int i = order[m];
            if (!miss[i]) continue;
            const int t = i >> 8;
            const uint8_t *cell = e->latest[i];
            // the same look as a cell given a slot just now: that one
            int s = idx_get(e, t, cell);
            if (s >= 0) {
                e->target_names[i] = (uint8_t)s;
                mine[t][s] = true;
                miss[i] = false;
                misses--;
                e->stats.hits++;
                continue;
            }
            const uint8_t own = front[i];
            const bool own_ok = !mine[t][own] && !mixed[t][own];
            if (pass == 0 && own_ok) continue;
            int pick = -1;
#if defined(NESCV_BESIDE) && !defined(NESCV_PICK_SCAN)
            // the same slot the loop below finds (the least recently shown
            // that is free, the lowest of equals), from the third's free
            // slots in that order: a picture new in every cell is 768
            // times 256 steps of that loop, 20 ms and more of the cart's time
            while (free_at[t] < free_n[t] && mine[t][free_list[t][free_at[t]]]) free_at[t]++;
            if (free_at[t] < free_n[t]) pick = free_list[t][free_at[t]++];
#else
            for (int k = 0; k < 256; k++)
                if (!busy[t][k] && !mine[t][k] && (pick < 0 || e->age[t][k] < e->age[t][pick])) pick = k;
#endif
            if (pick < 0) {
                e->stats.overflow++;
                if (!own_ok) {
                    // held: its slot keeps what it shows (mixed or mine:
                    // nothing rewrites it before the flip)
                    e->target_names[i] = own;
                    miss[i] = false;
                    misses--;
                    e->stats.held++;
                    continue;
                }
                pick = own;
            }
            const uint8_t *old = e->pat[t][pick];
            if (idx_get(e, t, old) == pick) idx_del(e, t, old);
            idx_put(e, t, cell, (uint8_t)pick);
            mine[t][pick] = true;
            miss[i] = false;
            misses--;
            e->target_names[i] = (uint8_t)pick;
            nescv_upload_t *u = &e->uploads[e->up_tail++];
            u->cell = (uint16_t)i;
            u->third = (uint8_t)t;
            u->slot = (uint8_t)pick;
        }
    e->target_index = e->latest_index;
    e->target_clash = e->latest_clash;
    e->target_backdrop = e->latest_backdrop;
    e->has_target = true;
    if (e->up_head == e->up_tail && e->target_backdrop == e->backdrop &&
        !memcmp(e->target_names, front, NESCV_CELLS)) {
        // the picture on screen already: nothing to write
        e->has_target = false;
        e->shown_index = e->target_index;
        e->stats.same++;
    }
}
#endif

static bool target_done(const nescv_t *e) {
    return e->has_target && e->up_head == e->up_tail &&
           !memcmp(e->target_names, e->vram_names[!e->front], NESCV_CELLS);
}

static void flip(nescv_t *e, list_t *l) {
    e->front ^= 1;
    l_reg(l, 2, (uint8_t)(6 + e->front));
    e->backdrop = e->target_backdrop;
    if (e->vram_r7 != e->backdrop) {
        l_reg(l, 7, e->backdrop);
        e->vram_r7 = e->backdrop;
    }
    const uint8_t *front = e->vram_names[e->front];
    for (int i = 0; i < NESCV_CELLS; i++) e->age[i >> 8][front[i]] = e->now;
    e->has_target = false;
    e->shown_index = e->target_index;
    e->stats.shown++;
    e->stats.flips++;
    e->stats.clash_rows += e->target_clash;
}

// bytes of a run that fit in what's left of the list (vblank part first)
static int room(list_t *l, int want) {
    if (fits(l, 1, (uint32_t)want)) return want;
    if (l->vblank) {
        if (!fits(l, 1, 1)) {
            l_end_vblank(l);
            return room(l, want);
        }
        // part of it now, in the vblank part's faster writes
        int k = (int)((VB_LIMIT - l->t - C_RUN_VB - C_END_VB) / C_BYTE_VB);
        return k < want ? k : want;
    }
    if (!fits(l, 1, 1)) return 0;
    int k = (int)((LIST_LIMIT - l->t - C_RUN_ACT - C_END) / C_BYTE_ACT);
    return k < want ? k : want;
}

#define NAME_GAP 4   // equal names that end a run: a new run costs 106 T, ~3.4 bytes
#define REFRESH_T 12000   // of a list's time, at most this a frame rewriting the screen as it is
#define REFRESH_STEPS (NESCV_ROWS + NESCV_CELLS)

NESCV_HOT static void work(nescv_t *e, list_t *l) {
    while (e->up_head != e->up_tail) {
        if (!fits(l, 2, 16)) {
            if (l->vblank && fits_active(l, 2, 16)) l_end_vblank(l);
            else return;
        }
        nescv_upload_t *u = &e->uploads[e->up_head];
        const uint8_t *cell = e->latest[u->cell];
        uint16_t at = (uint16_t)(u->third * 0x800 + u->slot * 8);
        l_run(l, PATTERNS + at, cell, 8);
        l_run(l, COLOURS + at, cell + 8, 8);
        memcpy(e->pat[u->third][u->slot], cell, 16);
        e->up_head++;
        e->stats.uploads++;
    }
    // the names, into the table not on screen
    uint8_t *back = e->vram_names[!e->front];
    const uint8_t *want = e->target_names;
    for (int i = 0; i < NESCV_CELLS;) {
        if (back[i] == want[i]) { i++; continue; }
        int last = i;
        for (int j = i + 1; j < NESCV_CELLS && j - i < 255 && j - last <= NAME_GAP; j++)
            if (back[j] != want[j]) last = j;
        int n = room(l, last - i + 1);
        if (n <= 0) return;
        l_run(l, (uint16_t)(NAMES[!e->front] + i), want + i, n);
        memcpy(back + i, want + i, (size_t)n);
        e->stats.name_bytes += (uint32_t)n;
        i += n;
    }
}

#ifndef NESCV_020
// What's on screen, written again as it is, with the time a list has left:
// a row of names, or a cell's slot (each slot once a pass), a step at a
// time; about half a second a pass. A write the VDP lost or took wrong
// (a bad contact on the cart's edge, say) is otherwise there until the
// cell changes, and a slot that's wrong spoils every cell that uses it.
NESCV_HOT static void refresh(nescv_t *e, list_t *l) {
    if (e->has_target && e->up_head != e->up_tail) return;   // the picture's own cells first
    const uint32_t end = l->t + REFRESH_T;
    const uint8_t *front = e->vram_names[e->front];
    for (int n = 0; n < REFRESH_STEPS; n++) {
        const int k = e->refresh_at;
        if (k < NESCV_ROWS) {
            if (!fits(l, 1, 32)) {
                if (l->vblank && fits_active(l, 1, 32)) l_end_vblank(l);
                else return;
            }
            if (l->t + C_RUN_ACT + 32 * C_BYTE_ACT > end) return;
            l_run(l, (uint16_t)(NAMES[e->front] + k * NESCV_COLS), front + k * NESCV_COLS, NESCV_COLS);
            e->stats.refreshed += NESCV_COLS;
        } else {
            const int c = k - NESCV_ROWS, t = c >> 8, slot = front[c];
            if (e->refresh_seen[t][slot] != e->refresh_pass) {
                if (!fits(l, 2, 16)) {
                    if (l->vblank && fits_active(l, 2, 16)) l_end_vblank(l);
                    else return;
                }
                if (l->t + 2 * C_RUN_ACT + 16 * C_BYTE_ACT > end) return;
                const uint16_t at = (uint16_t)(t * 0x800 + slot * 8);
                l_run(l, PATTERNS + at, e->pat[t][slot], 8);
                l_run(l, COLOURS + at, e->pat[t][slot] + 8, 8);
                e->refresh_seen[t][slot] = e->refresh_pass;
                e->stats.refreshed += 16;
            }
        }
        if (++e->refresh_at == REFRESH_STEPS) {
            e->refresh_at = 0;
            if (++e->refresh_pass == 0) {    // round again: none seen this pass
                memset(e->refresh_seen, 0, sizeof e->refresh_seen);
                e->refresh_pass = 1;
            }
        }
    }
}
#endif

uint64_t nescv_next_shown(const nescv_t *e) {
    return target_done(e) ? e->target_index : e->shown_index;
}

bool nescv_vram(nescv_t *e, uint16_t addr, const uint8_t *d, int n) {
    if (n < 1 || n > 255 || e->extra_n + 4 + n > NESCV_EXTRA_MAX) return false;
    uint8_t *b = e->extra + e->extra_n;
    b[0] = 1;
    b[1] = (uint8_t)addr;
    b[2] = (uint8_t)((addr >> 8) & 0x3F) | 0x40;
    b[3] = (uint8_t)n;
    memcpy(b + 4, d, (size_t)n);
    e->extra_n += (uint16_t)(4 + n);
    e->extra_t += C_RUN_VB + (uint32_t)n * C_BYTE_VB;
    return true;
}

bool nescv_reg(nescv_t *e, uint8_t r, uint8_t v) {
    if (r > 7 || e->extra_n + 3 > NESCV_EXTRA_MAX) return false;
    uint8_t *b = e->extra + e->extra_n;
    b[0] = 3;
    b[1] = v;
    b[2] = 0x80 | r;
    e->extra_n += 3;
    e->extra_t += C_REG;
    return true;
}

#ifdef NESCV_BESIDE
// What nescv_tv_frame() does when it finds a picture offered and none going
// in (the cells looked up, their slots chosen: up to 10 ms of the cart's
// time for a picture that is all new), done now instead, by a caller with
// time to spare before its next list is due. True if it did.
bool nescv_prepare(nescv_t *e) {
    if (e->has_target || !e->has_latest) return false;
    e->has_latest = false;
    if (e->latest_index > e->shown_index) start(e);
    return true;
}
#endif

NESCV_HOT size_t nescv_tv_frame(nescv_t *e, const uint8_t *psg, int npsg, uint8_t *out) {
    e->now++;
    list_t l = {.b = out, .n = 0, .t = C_START, .vblank = true};
    if (e->vram_r7 < 0) {                    // the first list: the border
        l_reg(&l, 7, e->backdrop);
        e->vram_r7 = e->backdrop;
    }
    if (npsg > 0) l_psg(&l, psg, npsg);
    if (e->extra_n) {                        // the caller's, before the flip: they go with it
        memcpy(l.b + l.n, e->extra, e->extra_n);
        l.n += e->extra_n;
        l.t += e->extra_t;
        e->extra_n = 0;
        e->extra_t = 0;
    }
    if (target_done(e)) flip(e, &l);
    if (!e->has_target && e->has_latest) {
        e->has_latest = false;
        if (e->latest_index > e->shown_index) start(e);
    }
    if (e->has_target) {
        work(e, &l);
        // done in the vertical blank part: show it now
        if (target_done(e) && l.vblank && l.t + 2 * C_REG + C_END_VB <= VB_LIMIT) flip(e, &l);
    }
#ifndef NESCV_020
    refresh(e, &l);
#endif
    l_end_vblank(&l);
    l.t += C_END;
    l.b[l.n++] = 0;
    e->stats.tv_frames++;
    e->stats.t_used += l.t;
    if (l.t > e->stats.t_max) e->stats.t_max = l.t;
    return l.n;
}

// ---- the sound -------------------------------------------------------------------

// a 4-bit linear volume as the PSG's attenuation (2 dB steps)
static const uint8_t ATT[16] = {15, 12, 9, 7, 6, 5, 4, 3, 3, 2, 2, 1, 1, 1, 0, 0};
#define TRI_ATT 2   // the triangle has no volume; a square wave of it is harsher, so a little down

void nescv_psg_init(nescv_psg_t *t) {
    for (int i = 0; i < 3; i++) t->sent_period[i] = -1;
    for (int i = 0; i < 4; i++) t->sent_att[i] = -1;
    t->sent_noise = -1;
}

static uint16_t tone(uint32_t n) {
    while (n > 1023) n = (n + 1) / 2;
    return (uint16_t)(n < 1 ? 1 : n);
}

int nescv_psg(nescv_psg_t *t, const nescv_apu_t *a, uint8_t *out) {
    int n = 0;
    for (int k = 0; k < 3; k++) {
        uint8_t att;
        uint16_t p;
        if (k < 2) {
            att = ATT[a->pulse_vol[k] & 15];
            p = tone(a->pulse_period[k] + 1u);
        } else {
            att = a->tri_on ? TRI_ATT : 15;
            p = tone(2u * (a->tri_period + 1u));
        }
        if (att != 15 && t->sent_period[k] != p) {
            out[n++] = (uint8_t)(0x80 | k << 5 | (p & 15));
            out[n++] = (uint8_t)((p >> 4) & 0x3F);
            t->sent_period[k] = p;
        }
        if (t->sent_att[k] != att) {
            out[n++] = (uint8_t)(0x90 | k << 5 | att);
            t->sent_att[k] = att;
        }
    }
    // the NES's noise shifts at 1789773 / {4 ... 4068} Hz (447 kHz to 440
    // Hz); the PSG's at 3579545 / (32 x 16, 32 or 64): 6991, 3496 or 1748 Hz
    uint8_t att = ATT[a->noise_vol & 15];
    if (att != 15) {
        uint8_t r = a->noise_rate & 15;      // 9: 7046 Hz, 11: 3523, 12: 2360
        uint8_t ctl = (uint8_t)(0xE4 | (r <= 9 ? 0 : r <= 11 ? 1 : 2));   // white noise
        if (t->sent_noise != ctl) {
            out[n++] = ctl;
            t->sent_noise = ctl;
        }
    }
    if (t->sent_att[3] != att) {
        out[n++] = (uint8_t)(0xF0 | att);
        t->sent_att[3] = att;
    }
    return n;
}
