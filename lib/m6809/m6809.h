// license:BSD-3-Clause
// copyright-holders:Nathan Woods
//
// m6809.h -- the Motorola 6809 as MAME 0.288's m6809 (src/devices/cpu/m6809)
// runs it: the same instructions (the undocumented ones too), flags, cycle
// counts, bus cycles, interrupt entry, SYNC and CWAI, and the same
// timeslice behaviour, so a board built with it can be checked against
// MAME instruction by instruction. README.md says where it came from and
// how it was proven; m6809.c has the license in full.
//
// The CPU's state is an m6809_t, so a board can have several. The
// instructions are a template, m6809.c: a plugin's .c file says how the CPU
// reaches its memory and includes it (m6809.c has the whole list):
//
//   #define M6809_RD(a)     main_rd(a)        // a byte read (a: uint16_t)
//   #define M6809_WR(a, v)  main_wr(a, v)     // a byte write
//   #include "m6809/m6809.c"                  // m6809_run(), m6809_reset(), m6809_step()
//
// For two CPUs wired differently, include it twice (from one file or two),
// each after its own connections and a name:
//   #define M6809_NAME(x) snd6809_##x   ->  snd6809_run(), snd6809_reset() ...
// CPUs wired alike can share one copy: the macros see the CPU as `c` (an
// m6809_t *), and c->user is the plugin's own.
//
// What a scheduler calls, with MAME's execute_run semantics:
//
//   <name>_run(c, cycles)     run a timeslice: instructions while cycles are
//                             left (the last may overrun); returns the cycles
//                             run (more than asked if it overran, fewer if
//                             aborted)
//   m6809_abort(c)            end the running timeslice after this
//                             instruction (abort_timeslice; a handler's)
//   m6809_adjust_icount(c,d)  add d to the cycles left (negative: wait states)
//   m6809_done(c)             cycles run so far in the running timeslice
//   m6809_now(c)              cycles since reset, to the start of the
//                             instruction running now (m6809_total: the same)
//   m6809_set_irq(c, state)   the IRQ, FIRQ and NMI lines: 1 asserted, 0 clear
//   m6809_set_firq(c, state)  (m6809_set_input(c, M6809_LINE_..., state) too).
//   m6809_set_nmi(c, state)   A line that rises while this CPU runs ends its
//                             timeslice, so the next instruction sees it.
//   <name>_reset(c)           the RESET pin (device_reset)
//   <name>_step(c)            one instruction inside the running timeslice
//                             (for M6809_SLOW_STEP; see m6809.c)
//   m6809_init(c)             power on: all zero (then <name>_reset)
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
    M6809_CC_C = 0x01, M6809_CC_V = 0x02, M6809_CC_Z = 0x04, M6809_CC_N = 0x08,
    M6809_CC_I = 0x10, M6809_CC_H = 0x20, M6809_CC_F = 0x40, M6809_CC_E = 0x80
};

// the input lines, for m6809_set_input (MAME's M6809_IRQ_LINE, M6809_FIRQ_LINE, INPUT_LINE_NMI)
enum { M6809_LINE_IRQ = 0, M6809_LINE_FIRQ = 1, M6809_LINE_NMI = 2 };

typedef struct m6809 {
    uint16_t pc, x, y, u, s;
    uint8_t a, b, dp, cc;
    union {
        struct {
            uint8_t irq, firq;      // the lines, asserted
            uint8_t nmi;            // an NMI waiting to be taken (MAME's m_nmi_asserted)
            uint8_t wait;           // 1: in SYNC, 2: in CWAI (its state stacked), 3: free run
        };
        uint32_t pending;           // any of them: the run loop looks at the lines
    };
    uint8_t nmi_line;               // the NMI line's level (its rising edge sets nmi)
    uint8_t lds;                    // S was loaded since reset (no NMI before)
    uint8_t wofs;                   // the last STA / STB's write: its cycle in the instruction
    int32_t icount;                 // cycles left in this timeslice
    int32_t slice;                  // the timeslice's length (less any abort)
    uint64_t end;                   // the cycle count it ends at
    void *user;                     // the plugin's
} m6809_t;

// power on (MAME's device_start): every register 0. Call <name>_reset after.
static inline void m6809_init(m6809_t *c) {
    void *user = c->user;
    memset(c, 0, sizeof *c);
    c->user = user;
}

// cycles since reset, to the start of the instruction now running (or, between
// runs, to where the last one ended)
static inline uint64_t m6809_now(const m6809_t *c) { return c->end - (uint64_t)(int64_t)c->icount; }
static inline uint64_t m6809_total(const m6809_t *c) { return m6809_now(c); }

// the cycles run so far in the running timeslice
static inline int m6809_done(const m6809_t *c) { return c->slice - c->icount; }

// abort_timeslice: the cycles left are given back, so the run ends after the
// instruction running now. (Between runs icount is 0 or less: nothing.)
static inline void m6809_abort(m6809_t *c) {
    if (c->icount > 0) {
        c->end -= (uint64_t)(int64_t)c->icount;
        c->slice -= c->icount;
        c->icount = 0;
    }
}

// cycles taken (negative) or given back: MAME's adjust_icount
static inline void m6809_adjust_icount(m6809_t *c, int delta) { c->icount += delta; }

// The lines (MAME's execute_set_input). The run loop looks at them when a run
// starts and after an instruction that changes the masks, so a line that
// rises while this CPU runs (a handler's) ends the run: the next run sees it
// at the next instruction, as MAME's does. IRQ and FIRQ are levels.
static inline void m6809_set_irq(m6809_t *c, int state) {
    if (state && !c->irq) m6809_abort(c);
    c->irq = state != 0;
}
static inline void m6809_set_firq(m6809_t *c, int state) {
    if (state && !c->firq) m6809_abort(c);
    c->firq = state != 0;
}
// NMI is taken on the line's rising edge, and only once S has been loaded
// (LDS, LEAS or TFR to S) since reset
static inline void m6809_set_nmi(m6809_t *c, int state) {
    if (state && !c->nmi_line && c->lds) {
        c->nmi = 1;
        m6809_abort(c);
    }
    c->nmi_line = state != 0;
}
static inline void m6809_set_input(m6809_t *c, int line, int state) {
    if (line == M6809_LINE_IRQ) m6809_set_irq(c, state);
    else if (line == M6809_LINE_FIRQ) m6809_set_firq(c, state);
    else if (line == M6809_LINE_NMI) m6809_set_nmi(c, state);
}

// What M6809_NAME(x) makes, for another file that calls them:
//   M6809_DECLARE(snd6809)  ->  int snd6809_run(m6809_t *, int); ...
#define M6809_DECLARE(p)                        \
    void p##_reset(m6809_t *c);                 \
    int p##_run(m6809_t *c, int cycles);        \
    int p##_step(m6809_t *c);
