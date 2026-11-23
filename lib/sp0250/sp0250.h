// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
//
// sp0250.h -- General Instrument's SP0250 LPC speech synthesizer, as MAME
// 0.288's sp0250_device (src/devices/sound/sp0250.cpp) makes its samples:
// the same FIFO, DRQ, pitch and repeat counts, noise generator and six
// two-pole filters, sample for sample, at the chip's own rate (clock / 312:
// 10,000 a second at the usual 3.12 MHz). MAME's PWM output mode isn't
// here: boards that use it are rare (Sega's G80 speech board doesn't).
//
//   #include "sp0250/sp0250.c"      // in one .c file; others include sp0250.h
//   sp0250_t sp;
//   sp0250_init(&sp);               // power on and reset: DRQ asserted
//   sp0250_write(&sp, byte);        // a byte into the FIFO (15 make a frame)
//   sp0250_drq(&sp)                 // 1 while the FIFO has room (the DRQ pin)
//   int8_t s = sp0250_next(&sp);    // the next sample, -64..63 (MAME: /128)
//   sp0250_render(&sp, buf, n);     // n of them
//
// Timing, as MAME has it: the FIFO's bytes and DRQ change only in
// sp0250_write and sp0250_next (when a frame is taken), so the board must
// make the samples up to "now" before each write and before it reads DRQ
// at a time that matters. MAME brings the stream up to date (makes every
// sample whose index is at most floor(now * rate)) before each write and
// on a timer at the chip's rate; sp.samples counts the samples made, so:
//   while (sp.samples <= floor(now * rate)) out[sp.samples] = sp0250_next(&sp);
// README.md says more and where it was proven.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// the sample rate for a clock (MAME: ROMCLOCK = clock / 2, a sample every
// 4 x 39 of those)
#define SP0250_RATE(clock) ((clock) / 2 / 156)

typedef struct {
    struct { int16_t F, B, z1, z2; } filter[6];
    int16_t amp;
    uint16_t lfsr;              // 15 bits, 0x7fff at power on
    uint8_t pitch, pcount, repeat, rcount;
    bool voiced;
    uint8_t fifo[15], fifo_pos;
    uint64_t samples;           // samples made since sp0250_init (the stream's position)
} sp0250_t;

void sp0250_init(sp0250_t *s);              // power on (device_start), then sp0250_reset
void sp0250_reset(sp0250_t *s);             // device_reset: a frame from the FIFO as it is, DRQ on
void sp0250_write(sp0250_t *s, uint8_t data);   // into the FIFO (a 16th byte is dropped)
int8_t sp0250_next(sp0250_t *s);            // the next sample, -64..63
void sp0250_render(sp0250_t *s, int8_t *out, int n);

// the DRQ line: 1 (ASSERT) while the FIFO isn't full
static inline int sp0250_drq(const sp0250_t *s) { return s->fifo_pos != 15; }
