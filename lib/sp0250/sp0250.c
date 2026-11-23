// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
//
// sp0250.c -- the SP0250 (sp0250.h): MAME 0.288's sp0250.cpp in C, from
// the Parrot Cart Star Trek plugin's copy. Include it from one .c file
// (several chips share it: each is an sp0250_t).
#include "sp0250.h"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define SP0250_HOT __attribute__((section(".time_critical.sp0250")))
#else
#define SP0250_HOT
#endif

// the amplitude: a 5-bit mantissa and a 3-bit exponent
static uint16_t sp0250_ga(uint8_t v) { return (uint16_t)((v & 0x1f) << (v >> 5)); }

// a filter coefficient from the chip's internal ROM (cf. the manual)
static int16_t sp0250_gc(uint8_t v) {
    static const uint16_t coefs[128] = {
        0, 9, 17, 25, 33, 41, 49, 57, 65, 73, 81, 89, 97, 105, 113, 121,
        129, 137, 145, 153, 161, 169, 177, 185, 193, 201, 209, 217, 225, 233, 241, 249,
        257, 265, 273, 281, 289, 297, 301, 305, 309, 313, 317, 321, 325, 329, 333, 337,
        341, 345, 349, 353, 357, 361, 365, 369, 373, 377, 381, 385, 389, 393, 397, 401,
        405, 409, 413, 417, 421, 425, 427, 429, 431, 433, 435, 437, 439, 441, 443, 445,
        447, 449, 451, 453, 455, 457, 459, 461, 463, 465, 467, 469, 471, 473, 475, 477,
        479, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495,
        496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511};
    const int16_t res = (int16_t)coefs[v & 0x7f];
    return (v & 0x80) ? res : (int16_t)-res;
}

// a frame from the FIFO (load_values): the FIFO empty again, DRQ on
static void sp0250_load_values(sp0250_t *s) {
    static const uint8_t bf[6][2] = {{0, 1}, {3, 4}, {6, 7}, {9, 10}, {11, 12}, {13, 14}};
    for (int f = 0; f < 6; f++) {
        s->filter[f].B = sp0250_gc(s->fifo[bf[f][0]]);
        s->filter[f].F = sp0250_gc(s->fifo[bf[f][1]]);
    }
    s->amp = (int16_t)sp0250_ga(s->fifo[2]);
    s->pitch = s->fifo[5];
    s->repeat = s->fifo[8] & 0x3f;
    s->voiced = (s->fifo[8] & 0x40) != 0;
    s->fifo_pos = 0;
    s->pcount = 0;
    s->rcount = 0;
    for (int f = 0; f < 6; f++) s->filter[f].z1 = s->filter[f].z2 = 0;
}

void sp0250_reset(sp0250_t *s) { sp0250_load_values(s); }

void sp0250_init(sp0250_t *s) {
    memset(s, 0, sizeof *s);
    s->lfsr = 0x7fff;
    sp0250_reset(s);
}

void sp0250_write(sp0250_t *s, uint8_t data) {
    if (s->fifo_pos != 15) s->fifo[s->fifo_pos++] = data;
    // (else MAME logs an overflow)
}

SP0250_HOT int8_t sp0250_next(sp0250_t *s) {
    if (s->rcount >= s->repeat) {
        if (s->fifo_pos == 15) sp0250_load_values(s);
        else {
            // while it waits for a frame, the chip runs "NOPs" with a
            // repeat count of 1 and the pitch unchanged
            s->repeat = 1;
            s->pcount = 0;
            s->rcount = 0;
        }
    }
    // the 15-bit LFSR (verified on the hardware), clocked every sample
    s->lfsr = (uint16_t)(s->lfsr ^ ((s->lfsr ^ (s->lfsr >> 1)) << 15));
    s->lfsr >>= 1;
    int16_t z0;
    if (s->voiced) z0 = s->pcount == 0 ? s->amp : 0;
    else z0 = (s->lfsr & 1) ? s->amp : (int16_t)-s->amp;
    for (int f = 0; f < 6; f++) {
        const int16_t o = (int16_t)(z0 + ((s->filter[f].z1 * s->filter[f].F) >> 8) + ((s->filter[f].z2 * s->filter[f].B) >> 9));
        s->filter[f].z2 = s->filter[f].z1;
        s->filter[f].z1 = o;
        z0 = o;
    }
    // the amplitude is 13 bits at most: 7 kept, and the filters can clip
    int dac = z0 >> 6;
    if (dac < -64) dac = -64;
    if (dac > 63) dac = 63;
    if (s->pcount++ == s->pitch) {
        s->pcount = 0;
        s->rcount++;
    }
    s->samples++;
    return (int8_t)dac;
}

void sp0250_render(sp0250_t *s, int8_t *out, int n) {
    for (int i = 0; i < n; i++) out[i] = sp0250_next(s);
}
