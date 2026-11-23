// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// m6800.h -- the Motorola 6800, 6802 and 6808 as MAME 0.288's m6800
// (src/devices/cpu/m6800/m6800.cpp, 6800ops.hxx) runs them: the same
// instructions, flags, cycle counts, interrupt entry and WAI, and the same
// timeslice behaviour, so a board built with it can be checked against MAME
// instruction by instruction. README.md says where it came from and how it
// was proven; m6800.c has the license in full. (The 6801/6803/63701
// family's extra instructions, ports and timer aren't here.)
//
// MAME's ways that a board has to keep to match it:
//   - A 6802 or 6808 divides its clock by 4, rounding up: MAME runs one on a
//     3,579,545 Hz crystal at 894,887 cycles a second, not 894,886.25
//     (m6800_cycles_per_second). A 6800 runs at its clock.
//   - The IRQ and NMI lines are looked at only when a timeslice starts and
//     after CLI, TAP, RTI and WAI. (m6800_set_irq and m6800_set_nmi end the
//     running timeslice when a line rises during it, as MAME's
//     synchronised set_input_line does.)
//   - The instruction after a TAP, or after a CLI that clears I, runs inside
//     it, before an interrupt can be taken.
//
// The CPU's state is an m6800_t, so a board can have several. The
// instructions are a template, m6800.c: a plugin's .c file says how the CPU
// reaches its memory and includes it (m6800.c has the whole list):
//
//   #define M6800_RD(a)     snd_rd(a)         // a byte read (a: uint16_t)
//   #define M6800_WR(a, v)  snd_wr(a, v)      // a byte write
//   #include "m6800/m6800.c"                  // m6800_run(), m6800_reset(), m6800_step()
//
// For two CPUs wired differently, include it twice (from one file or two),
// each after its own connections and a name:
//   #define M6800_NAME(x) snd2_##x   ->  snd2_run(), snd2_reset() ...
// CPUs wired alike can share one copy: the macros see the CPU as `c` (an
// m6800_t *), and c->user is the plugin's own.
//
// What a scheduler calls, with MAME's execute_run semantics:
//
//   <name>_run(c, cycles)     run a timeslice: the lines, then instructions
//                             until the cycles are used (at least one; the
//                             last may overrun); returns the cycles run (more
//                             than asked if it overran, fewer if aborted)
//   m6800_abort(c)            end the running timeslice after this
//                             instruction (abort_timeslice; a handler's)
//   m6800_adjust_icount(c,d)  add d to the cycles left (negative: wait states)
//   m6800_done(c)             cycles run so far in the running timeslice
//   m6800_now(c)              cycles since power on, to the start of the
//                             instruction running now (m6800_total: the same)
//   m6800_set_irq(c, state)   the IRQ and NMI lines: 1 asserted, 0 clear
//   m6800_set_nmi(c, state)   (m6800_set_input(c, M6800_LINE_..., state) too)
//   <name>_reset(c)           the RESET pin (device_reset)
//   <name>_step(c)            one instruction inside the running timeslice
//   m6800_init(c)             power on: all zero (then <name>_reset)
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
    M6800_CC_C = 0x01, M6800_CC_V = 0x02, M6800_CC_Z = 0x04, M6800_CC_N = 0x08,
    M6800_CC_I = 0x10, M6800_CC_H = 0x20
};

// the input lines, for m6800_set_input (MAME's M6800_IRQ_LINE, INPUT_LINE_NMI)
enum { M6800_LINE_IRQ = 0, M6800_LINE_NMI = 2 };

typedef struct m6800 {
    uint16_t pc, x, s;
    uint8_t a, b, cc;
    uint8_t irq;                    // the IRQ line, asserted
    uint8_t nmi;                    // an NMI waiting to be taken (MAME's m_nmi_pending)
    uint8_t nmi_line;               // the NMI line's level (its rising edge sets nmi)
    uint8_t wai;                    // in WAI, the state stacked
    int32_t icount;                 // cycles left in this timeslice
    int32_t slice;                  // the timeslice's length (less any abort)
    uint64_t end;                   // the cycle count it ends at
    void *user;                     // the plugin's
} m6800_t;

// MAME's cycles a second for a CPU on a clock of hz that divides it by div:
// (hz + div - 1) / div, rounded up (a 6802 or 6808: div 4; a 6800: 1)
static inline uint32_t m6800_cycles_per_second(uint32_t hz, uint32_t div) {
    return (uint32_t)(((uint64_t)hz + div - 1) / div);
}

// power on (MAME's device_start): every register 0. Call <name>_reset after.
static inline void m6800_init(m6800_t *c) {
    void *user = c->user;
    memset(c, 0, sizeof *c);
    c->user = user;
}

// cycles since power on, to the start of the instruction now running (or,
// between runs, to where the last one ended)
static inline uint64_t m6800_now(const m6800_t *c) { return c->end - (uint64_t)(int64_t)c->icount; }
static inline uint64_t m6800_total(const m6800_t *c) { return m6800_now(c); }

// the cycles run so far in the running timeslice
static inline int m6800_done(const m6800_t *c) { return c->slice - c->icount; }

// abort_timeslice: the cycles left are given back, so the run ends after the
// instruction running now. (Between runs icount is 0 or less: nothing.)
static inline void m6800_abort(m6800_t *c) {
    if (c->icount > 0) {
        c->end -= (uint64_t)(int64_t)c->icount;
        c->slice -= c->icount;
        c->icount = 0;
    }
}

// cycles taken (negative) or given back: MAME's adjust_icount
static inline void m6800_adjust_icount(m6800_t *c, int delta) { c->icount += delta; }

// The lines (MAME's execute_set_input). A line that rises while this CPU runs
// (a handler's) ends the run, so the next run's start sees it.
static inline void m6800_set_irq(m6800_t *c, int state) {
    if (state && !c->irq) m6800_abort(c);
    c->irq = state != 0;
}
// NMI is taken on the line's rising edge
static inline void m6800_set_nmi(m6800_t *c, int state) {
    if (state && !c->nmi_line) {
        c->nmi = 1;
        m6800_abort(c);
    }
    c->nmi_line = state != 0;
}
static inline void m6800_set_input(m6800_t *c, int line, int state) {
    if (line == M6800_LINE_IRQ) m6800_set_irq(c, state);
    else if (line == M6800_LINE_NMI) m6800_set_nmi(c, state);
}

// What M6800_NAME(x) makes, for another file that calls them:
//   M6800_DECLARE(snd2)  ->  int snd2_run(m6800_t *, int); ...
#define M6800_DECLARE(p)                        \
    void p##_reset(m6800_t *c);                 \
    int p##_run(m6800_t *c, int cycles);        \
    int p##_step(m6800_t *c);
