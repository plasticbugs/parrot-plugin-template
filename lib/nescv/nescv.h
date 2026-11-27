// SPDX-License-Identifier: LGPL-2.1
// lib/nescv: from the Parrot Cart NES plugin
// (github.com/plasticbugs/parrot-plugin-nes, dfd06f5), under its license,
// the GNU LGPL 2.1 (LICENSE here). The Neo Geo Pocket Color plugin added
// nescv_band_cols() and nescv_band_start() (a band of some columns only),
// NESCV_FAST_KEYS (keys compared a word at a time) and the key for pixels
// that are colours already (lut NULL). README.md says how to use it.
//
// nescv: the cart's side of a NES picture on the ColecoVision. Portable C
// (no pico-sdk), used by the NES plugin and by the Mac test.
//
// The console runs gbkernel (the kit's sdk/gbcv/kernel/gbkernel.asm): each TV
// frame it takes a command list from the cart and runs it (format and
// costs in its header). This turns NES pictures into those lists:
//
//   once:               nescv_init(e)
//   each NES picture:   nescv_bands(scroll_log, crop, quantum, &pic)
//                       nescv_offer(e, &pic, index)
//   each TV frame:      npsg = nescv_psg(&tr, &apu, psg)   (nesapu.h fills apu)
//                       len = nescv_tv_frame(e, psg, npsg, list)
//
// The picture is 256 x 192 of the NES's 256 x 240, at 1:1, in Graphics II:
// 32 x 24 cells of 8 x 8, each 8-pixel row in two of the TMS9918's 15
// colours. Each third of the screen has 256 pattern slots, and a cell
// already in a slot is shown by its name alone, so a picture costs 16 bytes
// for each cell new to its third plus the names that change. The NES draws
// its background from 8 x 8 tiles too: cut along the NES's own tile grid
// (nescv_bands), most cells repeat, and a scrolling picture is mostly new
// names.
//
// The names are double-buffered: tables at $1800 (R2 = 6) and $1C00 (R2 = 7).
// A picture's names go into the table not on screen and its cells into
// slots not on screen; when both are in, R2 flips in vertical blank. A
// picture that needs more than a frame's writes takes several frames, and
// the NES pictures in between are skipped: the console always shows a whole
// picture. A slot on screen is never given a look that the cells showing
// it don't all want, so when a third runs out of slots (a busy picture
// scrolling: a different cell in almost every place) a cell keeps its old
// look for a picture rather than another's appearing in it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NESCV_COLS  32
#define NESCV_ROWS  24
#define NESCV_CELLS (NESCV_COLS * NESCV_ROWS)
// The longest list: every command costs 23 T or more a byte it takes
// (gbkernel.asm's costs: a run 106 + 23n T for 4 + n bytes, PSG 75 + 52n for
// 2 + n, a register 114 for 3), and a list stays inside a frame's 58536 T.
#define NESCV_LIST_MAX 2600

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define NESCV_HOT __attribute__((section(".time_critical.nescv")))
#else
#define NESCV_HOT
#endif

// The NES's colours (0-63; the emphasis bits above them are ignored) as
// TMS9918 colours (1-15)
extern const uint8_t nescv_nes_colours[64];

// ---- a NES picture -------------------------------------------------------------
// pixels: the emulator's picture buffer, one byte a pixel, each byte a
// colour through lut (to 1-15: QuickNES's frame().palette through
// nescv_nes_colours). Band b (cell row b) is the 8 lines from
// line[b], 256 pixels from column col[b].
typedef struct {
    const uint8_t *pixels;
    long pitch;
    const uint8_t *lut;     // 256 entries
    uint8_t line[NESCV_ROWS];
    uint16_t col[NESCV_ROWS];
    uint8_t backdrop;       // the border's colour (R7), 1-15
} nescv_picture_t;

// The bands from QuickNES's scroll log (2 bytes a line: fine X, fine Y;
// 0xFF where the line had no background), for a buffer whose picture
// starts at column 8 (QuickNES draws the background from 8 - fine X).
// crop: the first of the 240 lines shown (0-48; 24 is the middle).
// quantum: 1, the plain crop (the background scrolls as the NES's does,
// every cell new); 8, cut along the background's tile grid (each band
// moves by up to 7 pixels across and its lines by up to 4 to meet it, so
// cells repeat, and the background scrolls 8 pixels at a time); 2 or 4,
// between: the background's phase in a cell is a multiple of it.
void nescv_bands(const uint8_t *scroll_log, int crop, int quantum, nescv_picture_t *p);

// ---- the encoder -----------------------------------------------------------------
typedef struct {
    uint8_t key[16];
    uint8_t slot;
    bool used;
} nescv_ent_t;

#define NESCV_HASH 512   // per third, a power of two
#define NESCV_EXTRA_MAX 160   // bytes of the caller's writes a list takes
#ifndef NESCV_MEMO_BITS  // (a plugin short of RAM sets fewer: 8 bytes a row)
#define NESCV_MEMO_BITS 12
#endif
#define NESCV_MEMO (1 << NESCV_MEMO_BITS)  // rows remembered

typedef struct {
    uint16_t cell;       // latest[cell] goes in
    uint8_t third, slot;
} nescv_upload_t;

typedef struct {
    uint32_t key;        // the row's eight colours, a nibble each (0: empty)
    uint8_t pat, colour, clash;
} nescv_memo_t;

typedef struct {
    uint32_t tv_frames, offered, shown, same, uploads, hits, overflow, clash_rows, index_resets;
    uint32_t overflow_shared;  // (no longer happens: a slot on screen only takes a look all its cells want)
    uint32_t held;             // cells that kept their old look for a picture (a third had no slot for them)
    uint32_t refreshed;        // bytes rewritten as they are (refresh())
    uint32_t name_bytes, flips, memo_hits, memo_misses;
    uint64_t t_used;
    uint32_t t_max;
} nescv_stats_t;

typedef struct {
    uint8_t vram_names[2][NESCV_CELLS];  // the two name tables in VRAM
    int front;                 // the table on screen (R2 = 6 + front)
    int vram_r7;               // -1 before the first list
    uint8_t backdrop;
    uint8_t pat[3][256][16];   // what VRAM holds in each slot
    uint32_t age[3][256];      // TV frame each slot was last put on screen
    nescv_ent_t index[3][NESCV_HASH];
    uint32_t now;

    // the newest NES picture, as cells
    bool has_latest;
    uint64_t latest_index;
    uint32_t latest_clash;
    uint8_t latest_backdrop;
    uint8_t latest[NESCV_CELLS][16];

    // the picture going in
    bool has_target;
    uint64_t target_index;
    uint32_t target_clash;
    uint8_t target_backdrop;
    uint8_t target_names[NESCV_CELLS];
    nescv_upload_t uploads[NESCV_CELLS];
    uint16_t up_head, up_tail;

    uint64_t shown_index;
    // the caller's writes for the next list's vertical blank part (nescv_vram, nescv_reg)
    uint8_t extra[NESCV_EXTRA_MAX];
    uint16_t extra_n;
    uint32_t extra_t;
    nescv_stats_t stats;
    nescv_memo_t memo[NESCV_MEMO];
    uint32_t band_clash;       // the picture's clashes so far (nescv_band)
    // the screen rewritten as it is, a little each frame (refresh()): the
    // next step (a row of names, then each cell's slot), and which slots
    // this pass has done (refresh_seen == refresh_pass)
    uint16_t refresh_at;
    uint8_t refresh_pass;
    uint8_t refresh_seen[3][256];
} nescv_t;

void nescv_init(nescv_t *e);
// true when the next nescv_tv_frame would take a new picture: the time to
// draw one (a NES frame not drawn costs the emulator less)
bool nescv_wants_picture(const nescv_t *e);
// index: the NES frame's number, from 1, rising. False (and the picture
// not taken) while the last one's cells are still going in: they are
// latest's. nescv_wants_picture() true means it takes it.
bool nescv_offer(nescv_t *e, const nescv_picture_t *p, uint64_t index);
// A picture a band at a time, as an emulator draws it, with no picture
// buffer: only while nescv_wants_picture() (which is then sure to take it),
// nescv_band() for each band r from 0 to 23 in turn (its 8 lines, 256
// pixels each, pitch apart, each byte a colour through lut), then
// nescv_band_done() with the picture's index and backdrop (as nescv_offer).
void nescv_band(nescv_t *e, int r, const uint8_t *px, long pitch, const uint8_t *lut);
// the same with four tables, for a picture dithered as it goes in: the
// pixel at (x, y) of the screen goes through luts[(y & 1) * 2 + (x & 1)]
// (the Master System plugin's grey background). Built with -DNESCV_LUTS.
void nescv_band_luts(nescv_t *e, int r, const uint8_t *px, long pitch, const uint8_t *const luts[4]);
void nescv_band_done(nescv_t *e, uint64_t index, uint8_t backdrop);
// (the Neo Geo Pocket Color plugin) part of a picture: nescv_band_start(),
// then nescv_band_cols() for the cells that change, columns c0 to c1 - 1
// of band r (px is the band's column 0, as nescv_band's), then
// nescv_band_done(). The cells left out keep what the last picture had.
void nescv_band_start(nescv_t *e);
void nescv_band_cols(nescv_t *e, int r, const uint8_t *px, long pitch, const uint8_t *lut, int c0, int c1);
// A picture made beside the encoder, over as many TV frames as it takes
// (the PC Engine plugin, which draws a frame after the game has run it).
// Built with -DNESCV_BESIDE. nescv_cells(): a band's 8 lines (as
// nescv_band()'s) as its 32 cells, into the caller's; returns the rows that
// lost a colour. nescv_offer_cells(): the whole picture's 768 cells, with
// the sum of those; false (and not taken) unless nescv_takes_cells(): the
// last picture offered has been started, and its cells have gone to the
// console.
uint32_t nescv_cells(nescv_t *e, const uint8_t *px, long pitch, const uint8_t *lut, uint8_t (*cells)[16]);
bool nescv_takes_cells(const nescv_t *e);
bool nescv_offer_cells(nescv_t *e, const uint8_t (*cells)[16], uint32_t clash, uint64_t index, uint8_t backdrop);
// The start of an offered picture (its cells looked up and given slots:
// the costly part of the nescv_tv_frame() that begins one) done now, when
// no picture is going in; true if it did. For the time a caller has to
// spare, so that its lists are never late for it.
bool nescv_prepare(nescv_t *e);

// this TV frame's list into out (NESCV_LIST_MAX bytes); returns its length
size_t nescv_tv_frame(nescv_t *e, const uint8_t *psg, int npsg, uint8_t *out);
// the picture the next list puts on screen at its start (its index: the
// one on screen now, or the one whose cells are all in), for writes that
// go with it (a pointer's sprite in a window that moved)
uint64_t nescv_next_shown(const nescv_t *e);

// ---- the caller's own writes: sprites, say ---------------------------------------
// The kernel leaves sprites off ($1B00 = $D0; sprite patterns at $3800, R1
// $E0: 8 x 8). These go into the next list's vertical blank part, ahead of
// the picture's, in the order given; false (and nothing queued) when they
// don't fit in what's left of NESCV_EXTRA_MAX.
bool nescv_vram(nescv_t *e, uint16_t addr, const uint8_t *d, int n);
bool nescv_reg(nescv_t *e, uint8_t r, uint8_t v);
// tests: index entries whose slot doesn't hold their cell
int nescv_check_index(const nescv_t *e);

// ---- the sound: the NES's APU as SN76489 bytes -------------------------------
// Each TV frame, the APU's state after the NES frame: pulse 1 and 2 go to
// tones 0 and 1, the triangle to tone 2, the noise to the noise channel; the
// DMC (samples) is left out. The PSG's clock is twice the NES CPU's, so a
// pulse's period p is the PSG's p + 1 and the triangle's (an octave lower)
// 2 (p + 1); notes below the PSG's lowest go up an octave.
typedef struct {
    uint16_t pulse_period[2];   // the 11-bit timer
    uint8_t pulse_vol[2];       // 0-15 (0 when the pulse is silent)
    uint16_t tri_period;
    bool tri_on;
    uint8_t noise_vol;          // 0-15
    uint8_t noise_rate;         // $400E's low 4 bits
} nescv_apu_t;

typedef struct {
    int32_t sent_period[3];     // -1: never sent
    int16_t sent_att[4];
    int16_t sent_noise;
} nescv_psg_t;

void nescv_psg_init(nescv_psg_t *t);
// the PSG bytes that bring the PSG to the APU's state; returns how many (<= 11)
int nescv_psg(nescv_psg_t *t, const nescv_apu_t *a, uint8_t *out);
