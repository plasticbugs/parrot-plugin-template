// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// m6800.c -- the 6800's instructions, a template (m6800.h has the API,
// README.md the story). A plugin's .c file defines how the CPU reaches its
// memory, then includes this; it defines <name>_reset, <name>_run and
// <name>_step, and can be included again with other macros for another CPU.
//
//   M6800_NAME(x)        the functions' names (default m6800_##x)
//   M6800_RD(a)          a byte read (a is a uint16_t). Required.
//   M6800_WR(a, v)       a byte write. Required.
//   M6800_FETCH(a)       optional: a byte of the program (an opcode or what
//                        follows it), where that is quicker than M6800_RD
//   M6800_IACK(line)     optional: an IRQ is being taken (MAME's
//                        standard_irq_callback; M6800_LINE_IRQ), just before
//                        its vector is read: a place to clear the line the way
//                        MAME's HOLD_LINE does
//   M6800_LOOP_SKIP      optional: run a delay loop (DECA, DECB or DEX, then
//                        BNE back to it) all but its last turns at once, as
//                        far as the timeslice goes. Nothing else can happen
//                        inside a 6800 timeslice, so the CPU comes out the
//                        same, as long as fetching the program has no side
//                        effects. Leave it out of a trace build: the turns it
//                        skips aren't traced.
//   M6800_TRACE(c)       optional: called before each instruction, c->pc at it
//   M6800_NO_TRACE       no M6800_TRACE from this copy
//   M6800_HOT            optional: an attribute for the run loop (a RAM section)
//
// The macros see the CPU as `c` (an m6800_t *), so one copy can serve several
// CPUs wired alike (c->user).
//
// Timing is MAME's: each instruction costs its cycles from MAME's
// cycles_6800 table (an undefined opcode: 4), charged when it finishes; an
// interrupt's entry costs 12 (4 out of WAI). A run looks at the lines first,
// then runs instructions until the cycles are used, at least one, as MAME's
// execute_run does. CLI, TAP, RTI and WAI look at the lines again, and TAP,
// and CLI when it clears I, run the next instruction inside themselves before
// that (MAME's execute_one in tap and cli). WAI with no interrupt to take
// uses the rest of the timeslice, then its own 9 cycles.
//
// Copyright (c) Aaron Giles and the MAME team; the changes for Parrot Cart,
// the Parrot Cart contributors.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#include "m6800.h"

#if !defined(M6800_RD) || !defined(M6800_WR)
#error "m6800.c: define M6800_RD(a) and M6800_WR(a, v) before including it"
#endif
#ifndef M6800_NAME
#define M6800_NAME(x) m6800_##x
#endif
#ifndef M6800_HOT
#define M6800_HOT
#endif
#ifndef M6800_IACK
#define M6800_IACK(line) ((void)0)
#endif

// MAME's cycles_6800 (an undefined opcode: 4), once a file
#ifndef M6800_CYCLES_TABLE_
#define M6800_CYCLES_TABLE_
static const uint8_t m6800_cycles_[256] = {
     4,  2,  4,  4,  4,  4,  2,  2,  4,  4,  2,  2,  2,  2,  2,  2,   // 00
     2,  2,  4,  4,  4,  4,  2,  2,  4,  2,  4,  2,  4,  4,  4,  4,   // 10
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,   // 20
     4,  4,  4,  4,  4,  4,  4,  4,  4,  5,  4, 10,  4,  4,  9, 12,   // 30
     2,  4,  4,  2,  2,  4,  2,  2,  2,  2,  2,  4,  2,  2,  4,  2,   // 40
     2,  4,  4,  2,  2,  4,  2,  2,  2,  2,  2,  4,  2,  2,  4,  2,   // 50
     7,  4,  4,  7,  7,  4,  7,  7,  7,  7,  7,  4,  7,  7,  4,  7,   // 60
     6,  4,  4,  6,  6,  4,  6,  6,  6,  6,  6,  4,  6,  6,  3,  6,   // 70
     2,  2,  2,  4,  2,  2,  2,  3,  2,  2,  2,  2,  3,  8,  3,  4,   // 80
     3,  3,  3,  4,  3,  3,  3,  4,  3,  3,  3,  3,  4,  6,  4,  5,   // 90
     5,  5,  5,  4,  5,  5,  5,  6,  5,  5,  5,  5,  6,  8,  6,  7,   // A0
     4,  4,  4,  4,  4,  4,  4,  5,  4,  4,  4,  4,  5,  9,  5,  6,   // B0
     2,  2,  2,  4,  2,  2,  2,  3,  2,  2,  2,  2,  4,  4,  3,  4,   // C0
     3,  3,  3,  4,  3,  3,  3,  4,  3,  3,  3,  3,  4,  4,  4,  5,   // D0
     5,  5,  5,  4,  5,  5,  5,  6,  5,  5,  5,  5,  4,  4,  6,  7,   // E0
     4,  4,  4,  4,  4,  4,  4,  5,  4,  4,  4,  4,  4,  4,  5,  6,   // F0
};
#endif

#define M_FN(n) M6800_NAME(n)

#define RD(a)       ((uint8_t)M6800_RD((uint16_t)(a)))
#ifndef M6800_FETCH
#define M6800_FETCH(a) M6800_RD(a)
#endif
#define FETCH(a)    ((uint8_t)M6800_FETCH((uint16_t)(a)))
#define WR(a, v)    M6800_WR((uint16_t)(a), (uint8_t)(v))

static inline uint16_t M_FN(x_rd16)(m6800_t *c, uint16_t a) {
    (void)c;
    uint8_t h = RD(a);
    return (uint16_t)(h << 8 | RD(a + 1));
}
static inline void M_FN(x_wr16)(m6800_t *c, uint16_t a, uint16_t v) {
    (void)c;
    WR(a, v >> 8);
    WR(a + 1, v);
}
#define RD16(a)     M_FN(x_rd16)(c, (uint16_t)(a))
#define WR16(a, v)  M_FN(x_wr16)(c, (uint16_t)(a), (uint16_t)(v))

// the operands (pc is the instruction's local)
#define IMM8()      FETCH(pc++)
#define IMM16()     (pc += 2, (uint16_t)(FETCH(pc - 2) << 8 | FETCH(pc - 1)))
#define EA_DIR()    ((uint16_t)FETCH(pc++))
#define EA_IDX()    ((uint16_t)(c->x + FETCH(pc++)))
#define EA_EXT()    IMM16()

// the stack: a push writes at S, then S goes down; a pull moves S up, then reads
#define PUSH8(v)    do { WR(c->s, v); c->s--; } while (0)
#define PUSH16(v)   do { uint16_t p_ = (uint16_t)(v); PUSH8(p_); PUSH8(p_ >> 8); } while (0)
static inline uint8_t M_FN(x_pull8)(m6800_t *c) { c->s++; return RD(c->s); }
static inline uint16_t M_FN(x_pull16)(m6800_t *c) {
    uint8_t h = M_FN(x_pull8)(c);
    return (uint16_t)(h << 8 | M_FN(x_pull8)(c));
}
#define PULL8()     M_FN(x_pull8)(c)
#define PULL16()    M_FN(x_pull16)(c)

// ---- flags (MAME's SET_* macros) ---------------------------------------------------
#define CCR (c->cc)
static inline void M_FN(x_nz8)(m6800_t *c, uint8_t r) {          // N Z; the rest kept
    CCR = (uint8_t)((CCR & ~(M6800_CC_N | M6800_CC_Z)) | (r & 0x80 ? M6800_CC_N : 0) | (r ? 0 : M6800_CC_Z));
}
static inline void M_FN(x_logic)(m6800_t *c, uint8_t r) {        // N Z, V = 0
    CCR &= (uint8_t)~M6800_CC_V;
    M_FN(x_nz8)(c, r);
}
static inline void M_FN(x_nz16)(m6800_t *c, uint16_t r) {        // N Z, V = 0
    CCR = (uint8_t)((CCR & ~(M6800_CC_N | M6800_CC_Z | M6800_CC_V)) | (r & 0x8000 ? M6800_CC_N : 0) |
                   (r ? 0 : M6800_CC_Z));
}
// SET_FLAGS8 after CLR_NZVC: N Z from r, V from a ^ b ^ r ^ (r >> 1), C from bit 8
static inline void M_FN(x_flags8)(m6800_t *c, unsigned a, unsigned b, unsigned r) {
    CCR = (uint8_t)((CCR & ~(M6800_CC_N | M6800_CC_Z | M6800_CC_V | M6800_CC_C)) | (r & 0x80 ? M6800_CC_N : 0) |
                   (r & 0xFF ? 0 : M6800_CC_Z) | (((a ^ b ^ r ^ (r >> 1)) & 0x80) >> 6) | ((r >> 8) & 1));
}
static inline uint8_t M_FN(x_sub)(m6800_t *c, uint8_t a, uint8_t b, unsigned cin) {
    unsigned r = (unsigned)(a - b - cin) & 0xFFFF;
    M_FN(x_flags8)(c, a, b, r);
    return (uint8_t)r;
}
static inline uint8_t M_FN(x_add)(m6800_t *c, uint8_t a, uint8_t b, unsigned cin) {
    unsigned r = a + b + cin;
    M_FN(x_flags8)(c, a, b, r);
    CCR = (uint8_t)((CCR & ~M6800_CC_H) | (((a ^ b ^ r) & 0x10) << 1));
    return (uint8_t)r;
}
// V = N ^ C (the shifts and rotates)
#define V_NXORC() do { if (((CCR >> 3) ^ CCR) & 1) CCR |= M6800_CC_V; } while (0)

// the read-modify-write operations ($40-$7F)
static inline uint8_t M_FN(x_neg)(m6800_t *c, uint8_t t) {
    unsigned r = (0u - t) & 0xFFFF;
    M_FN(x_flags8)(c, 0, t, r);
    return (uint8_t)r;
}
static inline uint8_t M_FN(x_com)(m6800_t *c, uint8_t t) {
    uint8_t r = (uint8_t)~t;
    M_FN(x_logic)(c, r);
    CCR |= M6800_CC_C;
    return r;
}
static inline uint8_t M_FN(x_lsr)(m6800_t *c, uint8_t t) {
    uint8_t r = t >> 1;
    CCR = (uint8_t)((CCR & ~(M6800_CC_N | M6800_CC_Z | M6800_CC_V | M6800_CC_C)) | (t & 1) | (r ? 0 : M6800_CC_Z));
    V_NXORC();
    return r;
}
static inline uint8_t M_FN(x_ror)(m6800_t *c, uint8_t t) {
    uint8_t r = (uint8_t)((CCR & M6800_CC_C) << 7 | t >> 1);
    CCR = (uint8_t)((CCR & ~(M6800_CC_V | M6800_CC_C)) | (t & 1));
    M_FN(x_nz8)(c, r);
    V_NXORC();
    return r;
}
static inline uint8_t M_FN(x_asr)(m6800_t *c, uint8_t t) {
    uint8_t r = (uint8_t)(t >> 1 | (t & 0x80));
    CCR = (uint8_t)((CCR & ~(M6800_CC_V | M6800_CC_C)) | (t & 1));
    M_FN(x_nz8)(c, r);
    V_NXORC();
    return r;
}
static inline uint8_t M_FN(x_asl)(m6800_t *c, uint8_t t) {
    unsigned r = (unsigned)t << 1;
    M_FN(x_flags8)(c, t, t, r);
    return (uint8_t)r;
}
static inline uint8_t M_FN(x_rol)(m6800_t *c, uint8_t t) {
    unsigned r = (unsigned)t << 1 | (CCR & M6800_CC_C);
    M_FN(x_flags8)(c, t, t, r);
    return (uint8_t)r;
}
static inline uint8_t M_FN(x_dec)(m6800_t *c, uint8_t t) {
    uint8_t r = (uint8_t)(t - 1);
    M_FN(x_logic)(c, r);
    if (r == 0x7F) CCR |= M6800_CC_V;
    return r;
}
static inline uint8_t M_FN(x_inc)(m6800_t *c, uint8_t t) {
    uint8_t r = (uint8_t)(t + 1);
    M_FN(x_logic)(c, r);
    if (r == 0x80) CCR |= M6800_CC_V;
    return r;
}
static inline void M_FN(x_tst)(m6800_t *c, uint8_t t) {
    CCR &= (uint8_t)~(M6800_CC_V | M6800_CC_C);
    M_FN(x_nz8)(c, t);
}
static inline uint8_t M_FN(x_clr)(m6800_t *c) {
    CCR = (uint8_t)((CCR & ~(M6800_CC_N | M6800_CC_V | M6800_CC_C)) | M6800_CC_Z);
    return 0;
}
// CPX: the 6800's flags, N and V from the high bytes' difference, Z from all 16 bits
static inline void M_FN(x_cpx)(m6800_t *c, uint16_t w) {
    unsigned hd = ((unsigned)(c->x >> 8) - (w >> 8)) & 0xFFFF;
    CCR = (uint8_t)(CCR & ~(M6800_CC_N | M6800_CC_Z | M6800_CC_V));
    CCR |= (uint8_t)((hd & 0x80) >> 4);
    CCR |= (uint8_t)((((c->x >> 8) ^ (w >> 8) ^ hd ^ (hd >> 1)) & 0x80) >> 6);
    if ((uint16_t)(c->x - w) == 0) CCR |= M6800_CC_Z;
}

// ---- interrupts ----------------------------------------------------------------------
// MAME's enter_interrupt: the state stacked (unless WAI did), I set, the
// vector, then its cycles off the count
static void M_FN(x_enter)(m6800_t *c, uint16_t vec) {
    int n;
    if (c->wai) {
        c->wai = 0;
        n = 4;
    } else {
        PUSH16(c->pc); PUSH16(c->x); PUSH8(c->a); PUSH8(c->b); PUSH8(CCR);
        n = 12;
    }
    CCR |= M6800_CC_I;
    c->pc = RD16(vec);
    c->icount -= n;
}
// MAME's check_irq_lines: an NMI waiting, or IRQ if I is clear
static void M_FN(x_check)(m6800_t *c) {
    if (c->nmi) {
        c->nmi = 0;
        M_FN(x_enter)(c, 0xFFFC);
    } else if (c->irq && !(CCR & M6800_CC_I)) {
        M6800_IACK(M6800_LINE_IRQ);
        M_FN(x_enter)(c, 0xFFF8);
    }
}

// the RESET pin (MAME's device_reset)
void M_FN(reset)(m6800_t *c) {
    CCR = 0xC0 | M6800_CC_I;
    c->pc = RD16(0xFFFE);
    c->wai = 0;
    c->nmi = 0;
    c->nmi_line = 0;
}

#define ALU8(opc, EXPR) \
    case opc:        m = IMM8(); EXPR; break; \
    case opc + 0x10: m = RD(EA_DIR()); EXPR; break; \
    case opc + 0x20: m = RD(EA_IDX()); EXPR; break; \
    case opc + 0x30: m = RD(EA_EXT()); EXPR; break;
#define ALU16(opc, EXPR) \
    case opc:        w = IMM16(); EXPR; break; \
    case opc + 0x10: w = RD16(EA_DIR()); EXPR; break; \
    case opc + 0x20: w = RD16(EA_IDX()); EXPR; break; \
    case opc + 0x30: w = RD16(EA_EXT()); EXPR; break;
// a store: the flags first, then the write (an immediate one writes the next byte)
#define ST8(opc, REG) \
    case opc:        M_FN(x_logic)(c, REG); ea = pc++; WR(ea, REG); break; \
    case opc + 0x10: M_FN(x_logic)(c, REG); ea = EA_DIR(); WR(ea, REG); break; \
    case opc + 0x20: M_FN(x_logic)(c, REG); ea = EA_IDX(); WR(ea, REG); break; \
    case opc + 0x30: M_FN(x_logic)(c, REG); ea = EA_EXT(); WR(ea, REG); break;
#define ST16(opc, REG) \
    case opc:        M_FN(x_nz16)(c, REG); ea = pc; pc += 2; WR16(ea, REG); break; \
    case opc + 0x10: M_FN(x_nz16)(c, REG); ea = EA_DIR(); WR16(ea, REG); break; \
    case opc + 0x20: M_FN(x_nz16)(c, REG); ea = EA_IDX(); WR16(ea, REG); break; \
    case opc + 0x30: M_FN(x_nz16)(c, REG); ea = EA_EXT(); WR16(ea, REG); break;
// read-modify-write: on A ($4x), B ($5x), indexed ($6x), extended ($7x)
#define RMW(opc, FN) \
    case opc:        c->a = FN(c, c->a); break; \
    case opc + 0x10: c->b = FN(c, c->b); break; \
    case opc + 0x20: ea = EA_IDX(); m = FN(c, RD(ea)); WR(ea, m); break; \
    case opc + 0x30: ea = EA_EXT(); m = FN(c, RD(ea)); WR(ea, m); break;
#define BRANCH(opc, cond) case opc: { int8_t o_ = (int8_t)IMM8(); if (cond) pc = (uint16_t)(pc + o_); break; }
#define F_C (CCR & M6800_CC_C)
#define F_V (CCR & M6800_CC_V)
#define F_Z (CCR & M6800_CC_Z)
#define F_N (CCR & M6800_CC_N)
#define F_NXV ((CCR >> 3 ^ CCR >> 1) & 1)

// A delay loop (M6800_LOOP_SKIP): at a DEC whose next bytes are BNE back to it
// (26 FD), with left turns to go before the count is 0 and per cycles a turn,
// all but the last turn that still starts inside the timeslice, at once. The
// DEC at hand then runs as usual.
#ifdef M6800_LOOP_SKIP
#define LOOP_SKIP(count, type, turns_at_0, per)                                       \
    do {                                                                              \
        if (top && c->icount > (per) && FETCH(pc) == 0x26 && FETCH(pc + 1) == 0xFD) { \
            uint32_t left_ = (count) ? (uint32_t)(count) : (turns_at_0);              \
            uint32_t k_ = (uint32_t)(c->icount - 1) / (per);                          \
            if (k_ > left_ - 1) k_ = left_ - 1;                                       \
            (count) = (type)((count) - k_);                                           \
            c->icount -= (int32_t)(k_ * (per));                                       \
        }                                                                             \
    } while (0)
#else
#define LOOP_SKIP(count, type, turns_at_0, per) ((void)top)
#endif

// One instruction (and, after TAP or CLI, the one it runs inside itself, and
// an interrupt CLI, TAP, RTI or WAI let in), its cycles off the count in
// MAME's order (execute_one). top: from the run loop.
static void M6800_HOT M_FN(x_insn)(m6800_t *c, bool top) {
#if defined(M6800_TRACE) && !defined(M6800_NO_TRACE)
    M6800_TRACE(c);                     // (tests: the registers before each instruction)
#endif
    uint16_t pc = c->pc, ea, w;
    uint8_t m;
    const unsigned op = FETCH(pc++);
    const int cyc = m6800_cycles_[op];
    switch (op) {
    // ---- $00-$1F
    case 0x01: break;                                               // NOP
    case 0x06:                                                      // TAP: the next one inside it
        CCR = c->a;
        c->pc = pc;
        M_FN(x_insn)(c, false);
        M_FN(x_check)(c);
        c->icount -= cyc;
        return;
    case 0x07: c->a = CCR; break;                                    // TPA
    case 0x08: c->x++; CCR = (uint8_t)((CCR & ~M6800_CC_Z) | (c->x ? 0 : M6800_CC_Z)); break;     // INX
    case 0x09:                                                      // DEX
        LOOP_SKIP(c->x, uint16_t, 65536u, 8);
        c->x--;
        CCR = (uint8_t)((CCR & ~M6800_CC_Z) | (c->x ? 0 : M6800_CC_Z));
        break;
    case 0x0A: CCR &= (uint8_t)~M6800_CC_V; break;                   // CLV
    case 0x0B: CCR |= M6800_CC_V; break;                             // SEV
    case 0x0C: CCR &= (uint8_t)~M6800_CC_C; break;                   // CLC
    case 0x0D: CCR |= M6800_CC_C; break;                             // SEC
    case 0x0E: {                                                    // CLI: if I was set, the next one inside it
        const bool i = CCR & M6800_CC_I;
        CCR &= (uint8_t)~M6800_CC_I;
        c->pc = pc;
        if (i) M_FN(x_insn)(c, false);
        M_FN(x_check)(c);
        c->icount -= cyc;
        return;
    }
    case 0x0F: CCR |= M6800_CC_I; break;                             // SEI
    case 0x10: c->a = M_FN(x_sub)(c, c->a, c->b, 0); break;         // SBA
    case 0x11: (void)M_FN(x_sub)(c, c->a, c->b, 0); break;          // CBA
    case 0x16: c->b = c->a; M_FN(x_logic)(c, c->b); break;          // TAB
    case 0x17: c->a = c->b; M_FN(x_logic)(c, c->a); break;          // TBA
    case 0x19: {                                                    // DAA (C kept from before, or set)
        const uint8_t a = c->a, msn = a & 0xF0, lsn = a & 0x0F;
        unsigned cf = 0;
        if (lsn > 9 || (CCR & M6800_CC_H)) cf |= 0x06;
        if (msn > 0x80 && lsn > 9) cf |= 0x60;
        if (msn > 0x90 || (CCR & M6800_CC_C)) cf |= 0x60;
        const unsigned r = cf + a;
        M_FN(x_logic)(c, (uint8_t)r);
        if (r & 0x100) CCR |= M6800_CC_C;
        c->a = (uint8_t)r;
        break;
    }
    case 0x1B: c->a = M_FN(x_add)(c, c->a, c->b, 0); break;         // ABA

    // ---- $20-$2F: branches
    BRANCH(0x20, true)
    BRANCH(0x21, false)
    BRANCH(0x22, !(F_C || F_Z))
    BRANCH(0x23, F_C || F_Z)
    BRANCH(0x24, !F_C)
    BRANCH(0x25, F_C)
    BRANCH(0x26, !F_Z)
    BRANCH(0x27, F_Z)
    BRANCH(0x28, !F_V)
    BRANCH(0x29, F_V)
    BRANCH(0x2A, !F_N)
    BRANCH(0x2B, F_N)
    BRANCH(0x2C, !F_NXV)
    BRANCH(0x2D, F_NXV)
    BRANCH(0x2E, !(F_NXV || F_Z))
    BRANCH(0x2F, F_NXV || F_Z)

    // ---- $30-$3F
    case 0x30: c->x = (uint16_t)(c->s + 1); break;                  // TSX
    case 0x31: c->s++; break;                                       // INS
    case 0x32: c->a = PULL8(); break;                               // PULA
    case 0x33: c->b = PULL8(); break;                               // PULB
    case 0x34: c->s--; break;                                       // DES
    case 0x35: c->s = (uint16_t)(c->x - 1); break;                  // TXS
    case 0x36: PUSH8(c->a); break;                                  // PSHA
    case 0x37: PUSH8(c->b); break;                                  // PSHB
    case 0x39: pc = PULL16(); break;                                // RTS
    case 0x3B:                                                      // RTI: then the lines
        CCR = PULL8();
        c->b = PULL8();
        c->a = PULL8();
        c->x = PULL16();
        c->pc = PULL16();
        M_FN(x_check)(c);
        c->icount -= cyc;
        return;
    case 0x3E:                                                      // WAI: the state stacked, then the lines
        PUSH16(pc); PUSH16(c->x); PUSH8(c->a); PUSH8(c->b); PUSH8(CCR);
        c->wai = 1;
        c->pc = pc;
        M_FN(x_check)(c);
        if (c->wai && c->icount > 0) c->icount = 0;                 // nothing yet: the rest of the slice goes by
        c->icount -= cyc;
        return;
    case 0x3F:                                                      // SWI
        PUSH16(pc); PUSH16(c->x); PUSH8(c->a); PUSH8(c->b); PUSH8(CCR);
        CCR |= M6800_CC_I;
        pc = RD16(0xFFFA);
        break;

    // ---- $40-$7F: read-modify-write
    RMW(0x40, M_FN(x_neg))
    RMW(0x43, M_FN(x_com))
    RMW(0x44, M_FN(x_lsr))
    RMW(0x46, M_FN(x_ror))
    RMW(0x47, M_FN(x_asr))
    RMW(0x48, M_FN(x_asl))
    RMW(0x49, M_FN(x_rol))
    case 0x4A: LOOP_SKIP(c->a, uint8_t, 256u, 6); c->a = M_FN(x_dec)(c, c->a); break;         // DECA
    case 0x5A: LOOP_SKIP(c->b, uint8_t, 256u, 6); c->b = M_FN(x_dec)(c, c->b); break;         // DECB
    case 0x6A: ea = EA_IDX(); m = M_FN(x_dec)(c, RD(ea)); WR(ea, m); break;
    case 0x7A: ea = EA_EXT(); m = M_FN(x_dec)(c, RD(ea)); WR(ea, m); break;
    RMW(0x4C, M_FN(x_inc))
    case 0x4D: M_FN(x_tst)(c, c->a); break;                         // TST
    case 0x5D: M_FN(x_tst)(c, c->b); break;
    case 0x6D: M_FN(x_tst)(c, RD(EA_IDX())); break;
    case 0x7D: M_FN(x_tst)(c, RD(EA_EXT())); break;
    case 0x6E: ea = EA_IDX(); pc = ea; break;                       // JMP
    case 0x7E: ea = EA_EXT(); pc = ea; break;
    case 0x4F: c->a = M_FN(x_clr)(c); break;                        // CLR (in memory: read, then write)
    case 0x5F: c->b = M_FN(x_clr)(c); break;
    case 0x6F: ea = EA_IDX(); (void)RD(ea); m = M_FN(x_clr)(c); WR(ea, m); break;
    case 0x7F: ea = EA_EXT(); (void)RD(ea); m = M_FN(x_clr)(c); WR(ea, m); break;

    // ---- $80-$BF: A; $C0-$FF: B
    ALU8(0x80, c->a = M_FN(x_sub)(c, c->a, m, 0))                   // SUBA
    ALU8(0x81, (void)M_FN(x_sub)(c, c->a, m, 0))                    // CMPA
    ALU8(0x82, c->a = M_FN(x_sub)(c, c->a, m, F_C))                 // SBCA
    ALU8(0x84, c->a &= m; M_FN(x_logic)(c, c->a))                   // ANDA
    ALU8(0x85, M_FN(x_logic)(c, c->a & m))                          // BITA
    ALU8(0x86, c->a = m; M_FN(x_logic)(c, m))                       // LDAA
    ST8(0x87, c->a)                                                 // STAA
    ALU8(0x88, c->a ^= m; M_FN(x_logic)(c, c->a))                   // EORA
    ALU8(0x89, c->a = M_FN(x_add)(c, c->a, m, F_C))                 // ADCA
    ALU8(0x8A, c->a |= m; M_FN(x_logic)(c, c->a))                   // ORAA
    ALU8(0x8B, c->a = M_FN(x_add)(c, c->a, m, 0))                   // ADDA
    ALU16(0x8C, M_FN(x_cpx)(c, w))                                  // CPX
    case 0x8D: { int8_t o = (int8_t)IMM8(); PUSH16(pc); pc = (uint16_t)(pc + o); break; }      // BSR
    case 0x9D: ea = EA_DIR(); PUSH16(pc); pc = ea; break;           // JSR (direct too, as MAME has it)
    case 0xAD: ea = EA_IDX(); PUSH16(pc); pc = ea; break;
    case 0xBD: ea = EA_EXT(); PUSH16(pc); pc = ea; break;
    ALU16(0x8E, c->s = w; M_FN(x_nz16)(c, w))                       // LDS
    ST16(0x8F, c->s)                                                // STS

    ALU8(0xC0, c->b = M_FN(x_sub)(c, c->b, m, 0))                   // SUBB
    ALU8(0xC1, (void)M_FN(x_sub)(c, c->b, m, 0))                    // CMPB
    ALU8(0xC2, c->b = M_FN(x_sub)(c, c->b, m, F_C))                 // SBCB
    ALU8(0xC4, c->b &= m; M_FN(x_logic)(c, c->b))                   // ANDB
    ALU8(0xC5, M_FN(x_logic)(c, c->b & m))                          // BITB
    ALU8(0xC6, c->b = m; M_FN(x_logic)(c, m))                       // LDAB
    ST8(0xC7, c->b)                                                 // STAB
    ALU8(0xC8, c->b ^= m; M_FN(x_logic)(c, c->b))                   // EORB
    ALU8(0xC9, c->b = M_FN(x_add)(c, c->b, m, F_C))                 // ADCB
    ALU8(0xCA, c->b |= m; M_FN(x_logic)(c, c->b))                   // ORAB
    ALU8(0xCB, c->b = M_FN(x_add)(c, c->b, m, 0))                   // ADDB
    ALU16(0xCE, c->x = w; M_FN(x_nz16)(c, w))                       // LDX
    ST16(0xCF, c->x)                                                // STX

    // ---- undefined (MAME's illegl2 and illegl3): their operand bytes go by
    case 0x61: case 0x62: case 0x65: case 0x6B:
    case 0x83: case 0x93: case 0xA3: case 0xC3: case 0xD3: case 0xE3:
    case 0xDC: case 0xDD: case 0xEC: case 0xED:
        pc++;
        break;
    case 0x71: case 0x72: case 0x75: case 0x7B:
    case 0xB3: case 0xF3: case 0xCC: case 0xCD: case 0xFC: case 0xFD:
        pc += 2;
        break;
    default:                                                        // (MAME's illegl1: nothing)
        break;
    }
    c->pc = pc;
    c->icount -= cyc;
}

// Runs a timeslice: the lines, then instructions until the cycles are used (at
// least one); returns the cycles run.
int M6800_HOT M_FN(run)(m6800_t *c, int cycles) {
    c->end = m6800_now(c) + (uint64_t)(int64_t)cycles;
    c->icount = cycles;
    c->slice = cycles;
    if (cycles <= 0) return 0;
    M_FN(x_check)(c);                   // (MAME's check_irq_lines at the slice's start)
    do {
        if (c->wai) {                   // waiting: the rest of the slice goes by
            if (c->icount > 0) c->icount = 0;
            break;
        }
        M_FN(x_insn)(c, true);
    } while (c->icount > 0);
    return c->slice - c->icount;
}

// One instruction (with what TAP or CLI runs inside it) inside the running
// timeslice: its cycles come off the slice's count, and a handler's
// m6800_abort ends the slice after it. No look at the lines first. Returns its
// cycles.
int M_FN(step)(m6800_t *c) {
    if (c->wai) return 0;
    const int32_t before = c->icount;
    M_FN(x_insn)(c, false);
    return before - c->icount;
}

#undef RD
#undef FETCH
#undef M6800_FETCH
#undef WR
#undef RD16
#undef WR16
#undef IMM8
#undef IMM16
#undef EA_DIR
#undef EA_IDX
#undef EA_EXT
#undef PUSH8
#undef PUSH16
#undef PULL8
#undef PULL16
#undef CCR
#undef V_NXORC
#undef ALU8
#undef ALU16
#undef ST8
#undef ST16
#undef RMW
#undef BRANCH
#undef F_C
#undef F_V
#undef F_Z
#undef F_N
#undef F_NXV
#undef LOOP_SKIP
#undef M_FN
#undef M6800_NAME
#undef M6800_RD
#undef M6800_WR
#undef M6800_HOT
#undef M6800_IACK
#undef M6800_LOOP_SKIP
#undef M6800_NO_TRACE
