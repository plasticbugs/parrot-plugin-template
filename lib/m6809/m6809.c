// license:BSD-3-Clause
// copyright-holders:Nathan Woods
//
// m6809.c -- the 6809's instructions, a template (m6809.h has the API,
// README.md the story). A plugin's .c file defines how the CPU reaches its
// memory, then includes this; it defines <name>_reset, <name>_run and
// <name>_step, and can be included again with other macros for another CPU.
//
//   M6809_NAME(x)        the functions' names (default m6809_##x)
//   M6809_RD(a)          a byte read (a is a uint16_t). Required.
//   M6809_WR(a, v)       a byte write. Required.
//   M6809_FETCH(a)       optional: a byte of the program (an opcode or what
//                        follows it), where that is quicker than M6809_RD
//   M6809_FETCH_BASE(pc) optional: a uintptr_t worked out once an instruction
//                        (fbase, from the address of its first byte), for
//                        M6809_FETCH to use: (*(const uint8_t *)(fbase + (a)))
//                        with fbase = the page's memory less the page's address
//   M6809_SLOW_STEP(c)   optional, with M6809_FETCH_BASE: an instruction whose
//                        fbase is 0 isn't run here but by this (c->pc in and
//                        out; it keeps c's clock itself): another copy's
//                        <name>_step, built with the slow bus
//   M6809_DUMMY(a)       optional: a bus cycle that reads address a for
//                        nothing (MAME's dummy_read_opcode_arg and dummy_vma,
//                        in MAME's places), for a bus with something watching it
//   M6809_DUMMY_VMA(n)   optional, with it: n such cycles at $FFFF (default:
//                        M6809_DUMMY(0xFFFF) n times)
//   M6809_IACK(line)     optional: an interrupt is being taken (MAME's
//                        standard_irq_callback), M6809_LINE_IRQ, _FIRQ or _NMI,
//                        just before its vector is read: a place to clear a
//                        line the way MAME's HOLD_LINE does
//   M6809_TRACE(c)       optional: called before each instruction, c->pc at it
//   M6809_NO_TRACE       no M6809_TRACE from this copy
//   M6809_HOT            optional: an attribute for the run loop (a RAM section)
//
// The macros see the CPU as `c` (an m6809_t *), so one copy can serve several
// CPUs wired alike (c->user). In the run loop the PC is a local, in c->pc only
// around the calls that want it there.
//
// Timing is MAME's: an instruction's cycles are the bus cycles MAME counts
// (each read, write and dummy cycle), charged when it finishes. The lines are
// looked at when a run starts and after an instruction that changes the masks
// (ANDCC, TFR/EXG, PULS/PULU, RTI, CWAI, SYNC); m6809_set_irq and the others
// end the run when a line rises during it, so the next instruction boundary
// sees it, as MAME's check before every instruction does. MAME can stop a 6809
// in the middle of an instruction at the end of a timeslice; this one runs the
// whole instruction and counts the overrun into the next slice.
//
// Undocumented opcodes do what MAME 0.288's do. One difference: a $10 or $11
// prefix whose next byte isn't on its page makes MAME fetch a new opcode in
// the same instruction; here it does the same, except under M6809_SLOW_STEP
// when that byte's page is a slow one, where the prefix and its byte end an
// instruction of their own (2 cycles) and the next byte starts one.
//
// Copyright (c) Nathan Woods and the MAME team; the changes for Parrot Cart,
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
#include "m6809.h"

#if !defined(M6809_RD) || !defined(M6809_WR)
#error "m6809.c: define M6809_RD(a) and M6809_WR(a, v) before including it"
#endif
#ifndef M6809_NAME
#define M6809_NAME(x) m6809_##x
#endif
#ifndef M6809_HOT
#define M6809_HOT
#endif
#ifndef M6809_IACK
#define M6809_IACK(line) ((void)0)
#endif

// N and Z for a byte (the loads, stores and logic), once a file
#ifndef M6809_NZ_TABLE_
#define M6809_NZ_TABLE_
#define M6809_N8_ M6809_CC_N, M6809_CC_N, M6809_CC_N, M6809_CC_N, M6809_CC_N, M6809_CC_N, M6809_CC_N, M6809_CC_N
static const uint8_t m6809_nz_[256] = {
    M6809_CC_Z, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    M6809_N8_, M6809_N8_, M6809_N8_, M6809_N8_, M6809_N8_, M6809_N8_, M6809_N8_, M6809_N8_,
    M6809_N8_, M6809_N8_, M6809_N8_, M6809_N8_, M6809_N8_, M6809_N8_, M6809_N8_, M6809_N8_,
};
#undef M6809_N8_
#endif

#define M_FN(n) M6809_NAME(n)

#define RD(a)       ((uint8_t)M6809_RD((uint16_t)(a)))
#ifndef M6809_FETCH
#define M6809_FETCH(a) M6809_RD(a)
#endif
#ifndef M6809_FETCH_BASE
#define M6809_FETCH_BASE(pc) 0
#endif
#define FETCH(a)    ((uint8_t)M6809_FETCH((uint16_t)(a)))
#define WR(a, v)    M6809_WR((uint16_t)(a), (uint8_t)(v))

// the dummy cycles: DA(d) reads the byte d past the PC, DV(n) is n cycles at
// $FFFF, DM(a) reads a
#ifdef M6809_DUMMY
#ifndef M6809_DUMMY_VMA
static inline void M_FN(x_vma)(m6809_t *c, int n) {
    (void)c;
    while (n-- > 0) M6809_DUMMY(0xFFFF);
}
#define M6809_DUMMY_VMA(n) M_FN(x_vma)(c, n)
#endif
#define DM(a)       ((void)M6809_DUMMY((uint16_t)(a)))
#define DA(d)       ((void)M6809_DUMMY((uint16_t)(pc + (d))))
#define DV(n)       ((void)M6809_DUMMY_VMA(n))
#else
#define DM(a)       ((void)0)
#define DA(d)       ((void)0)
#define DV(n)       ((void)0)
#endif

static inline uint16_t M_FN(x_rd16)(m6809_t *c, uint16_t a) {
    (void)c;
    uint8_t h = RD(a);
    return (uint16_t)(h << 8 | RD(a + 1));
}
static inline void M_FN(x_wr16)(m6809_t *c, uint16_t a, uint16_t v) {
    (void)c;
    WR(a, v >> 8);                  // the high byte first, as the 6809 does
    WR(a + 1, v);
}
#define RD16(a)     M_FN(x_rd16)(c, (uint16_t)(a))
#define WR16(a, v)  M_FN(x_wr16)(c, (uint16_t)(a), (uint16_t)(v))
#define IMM8()      FETCH(pc++)
#define IMM16()     (pc += 2, (uint16_t)(FETCH(pc - 2) << 8 | FETCH(pc - 1)))
#define D()         ((uint16_t)(c->a << 8 | c->b))
#define SET_D(v)    do { uint16_t d_ = (uint16_t)(v); c->a = (uint8_t)(d_ >> 8); c->b = (uint8_t)d_; } while (0)

#define PUSHS8(v)   WR(--c->s, v)
#define PUSHS16(v)  do { uint16_t p_ = (uint16_t)(v); WR(--c->s, p_); WR(--c->s, p_ >> 8); } while (0)
#define PULLS8()    RD(c->s++)
#define PUSHU8(v)   WR(--c->u, v)
#define PUSHU16(v)  do { uint16_t p_ = (uint16_t)(v); WR(--c->u, p_); WR(--c->u, p_ >> 8); } while (0)
#define PULLU8()    RD(c->u++)

static inline uint16_t M_FN(x_pulls16)(m6809_t *c) { uint16_t v = RD16(c->s); c->s += 2; return v; }
static inline uint16_t M_FN(x_pullu16)(m6809_t *c) { uint16_t v = RD16(c->u); c->u += 2; return v; }
#define PULLS16()   M_FN(x_pulls16)(c)
#define PULLU16()   M_FN(x_pullu16)(c)

// ---- flags (MAME's set_flags) ---------------------------------------------------
static inline void M_FN(x_nz8)(m6809_t *c, uint8_t r) {      // N Z, V = 0
    c->cc = (uint8_t)((c->cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_V)) | m6809_nz_[r]);
}
static inline void M_FN(x_nz16)(m6809_t *c, uint16_t r) {    // N Z, V = 0
    c->cc = (uint8_t)((c->cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_V)) | (r & 0x8000 ? M6809_CC_N : 0) |
                      (r ? 0 : M6809_CC_Z));
}
static inline uint8_t M_FN(x_add8)(m6809_t *c, uint8_t a, uint8_t b, unsigned cin) {
    unsigned r = a + b + cin;
    uint8_t cc = c->cc & ~(M6809_CC_H | M6809_CC_N | M6809_CC_Z | M6809_CC_V | M6809_CC_C);
    if ((a ^ b ^ r) & 0x10) cc |= M6809_CC_H;
    if (r & 0x80) cc |= M6809_CC_N;
    if (!(r & 0xFF)) cc |= M6809_CC_Z;
    if ((a ^ r) & (b ^ r) & 0x80) cc |= M6809_CC_V;
    if (r & 0x100) cc |= M6809_CC_C;
    c->cc = cc;
    return (uint8_t)r;
}
static inline uint8_t M_FN(x_sub8)(m6809_t *c, uint8_t a, uint8_t b, unsigned cin) {
    unsigned r = a - b - cin;
    uint8_t cc = c->cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_V | M6809_CC_C);
    if (r & 0x80) cc |= M6809_CC_N;
    if (!(r & 0xFF)) cc |= M6809_CC_Z;
    if ((a ^ b) & (a ^ r) & 0x80) cc |= M6809_CC_V;
    if (r & 0x100) cc |= M6809_CC_C;
    c->cc = cc;
    return (uint8_t)r;
}
static inline uint16_t M_FN(x_add16)(m6809_t *c, uint16_t a, uint16_t b) {
    uint32_t r = (uint32_t)a + b;
    uint8_t cc = c->cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_V | M6809_CC_C);
    if (r & 0x8000) cc |= M6809_CC_N;
    if (!(r & 0xFFFF)) cc |= M6809_CC_Z;
    if ((a ^ r) & (b ^ r) & 0x8000) cc |= M6809_CC_V;
    if (r & 0x10000) cc |= M6809_CC_C;
    c->cc = cc;
    return (uint16_t)r;
}
static inline uint16_t M_FN(x_sub16)(m6809_t *c, uint16_t a, uint16_t b) {
    uint32_t r = (uint32_t)a - b;
    uint8_t cc = c->cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_V | M6809_CC_C);
    if (r & 0x8000) cc |= M6809_CC_N;
    if (!(r & 0xFFFF)) cc |= M6809_CC_Z;
    if ((a ^ b) & (a ^ r) & 0x8000) cc |= M6809_CC_V;
    if (r & 0x10000) cc |= M6809_CC_C;
    c->cc = cc;
    return (uint16_t)r;
}
// the read-modify-write group ($00-$0F, $40-$7F) by the opcode's low nibble
static inline uint8_t M_FN(x_rmw)(m6809_t *c, unsigned op, uint8_t m) {
    uint8_t r, cc = c->cc;
    switch (op & 15) {
    case 0x2:                           // (XNC: COM if C, else NEG)
        if (cc & M6809_CC_C) goto com;
        /* fall through */
    case 0x0: case 0x1:                 // NEG
        return M_FN(x_sub8)(c, 0, m, 0);
    case 0x3: com:                      // COM
        r = (uint8_t)~m;
        M_FN(x_nz8)(c, r);
        c->cc |= M6809_CC_C;
        return r;
    case 0x4: case 0x5:                 // LSR
        r = m >> 1;
        c->cc = (uint8_t)((cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_C)) | (m & 1) | (r ? 0 : M6809_CC_Z));
        return r;
    case 0x6:                           // ROR
        r = (uint8_t)(m >> 1 | (cc & M6809_CC_C) << 7);
        c->cc = (uint8_t)((cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_C)) | (m & 1) | m6809_nz_[r]);
        return r;
    case 0x7:                           // ASR
        r = (uint8_t)(m >> 1 | (m & 0x80));
        c->cc = (uint8_t)((cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_C)) | (m & 1) | m6809_nz_[r]);
        return r;
    case 0x8:                           // ASL (H stays)
        r = M_FN(x_add8)(c, m, m, 0);
        c->cc = (uint8_t)((c->cc & ~M6809_CC_H) | (cc & M6809_CC_H));
        return r;
    case 0x9:                           // ROL (H stays)
        r = M_FN(x_add8)(c, m, m, cc & M6809_CC_C);
        c->cc = (uint8_t)((c->cc & ~M6809_CC_H) | (cc & M6809_CC_H));
        return r;
    case 0xB:                           // (XDEC: DEC, and C = the operand wasn't 0)
        cc = (uint8_t)((cc & ~M6809_CC_C) | (m ? M6809_CC_C : 0));
        /* fall through */
    case 0xA:                           // DEC
        r = (uint8_t)(m - 1);
        c->cc = (uint8_t)((cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_V)) | m6809_nz_[r] | (m == 0x80 ? M6809_CC_V : 0));
        return r;
    case 0xC:                           // INC
        r = (uint8_t)(m + 1);
        c->cc = (uint8_t)((cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_V)) | m6809_nz_[r] | (m == 0x7F ? M6809_CC_V : 0));
        return r;
    case 0xD:                           // TST
        M_FN(x_nz8)(c, m);
        return m;
    case 0xE:                           // (XCLR, $4E/$5E: CLR that keeps C; $xE in memory is JMP)
        c->cc = (uint8_t)((cc & ~(M6809_CC_N | M6809_CC_V)) | M6809_CC_Z);
        return 0;
    default:                            // CLR
        c->cc = (uint8_t)((cc & ~(M6809_CC_N | M6809_CC_V | M6809_CC_C)) | M6809_CC_Z);
        return 0;
    }
}

// ---- indexed addressing: the effective address, with the bytes it took after
// the postbyte (<< 16) and its cycles past the two of ,R (<< 24, signed) -----------
static uint32_t M6809_HOT M_FN(x_indexed)(m6809_t *c, uint16_t pc) {
    uintptr_t fbase = M6809_FETCH_BASE(pc);
    (void)fbase;
    uint8_t pb = FETCH(pc++);
    uint16_t *r;
    switch (pb >> 5 & 3) {
    case 0: r = &c->x; break;
    case 1: r = &c->y; break;
    case 2: r = &c->u; break;
    default: r = &c->s; break;
    }
    if (!(pb & 0x80)) {                 // 5-bit offset
        DA(0); DV(1);
        return (uint16_t)(*r + ((pb & 0x10) ? (int)(pb & 0x1F) - 32 : (pb & 0x1F))) | 1u << 24;
    }
    uint16_t ea;
    int cyc = 0;
    uint32_t bytes = 0;
    switch (pb & 15) {
    case 0x0: ea = *r; *r += 1; cyc = 2; DA(0); DV(2); break;
    case 0x1: ea = *r; *r += 2; cyc = 3; DA(0); DV(3); break;
    case 0x2: *r -= 1; ea = *r; cyc = 2; DA(0); DV(2); break;
    case 0x3: *r -= 2; ea = *r; cyc = 3; DA(0); DV(3); break;
    case 0x4: ea = *r; DA(0); break;
    case 0x5: ea = (uint16_t)(*r + (int8_t)c->b); cyc = 1; DA(0); DV(1); break;
    case 0x6: ea = (uint16_t)(*r + (int8_t)c->a); cyc = 1; DA(0); DV(1); break;
    case 0x8: ea = (uint16_t)(*r + (int8_t)FETCH(pc)); bytes = 1; cyc = 1; DA(1); break;
    case 0x9: ea = (uint16_t)(*r + (FETCH(pc) << 8 | FETCH(pc + 1))); bytes = 2; cyc = 4; DV(3); break;
    case 0xB: ea = (uint16_t)(*r + D()); cyc = 4; DA(0); DA(1); DV(3); break;
    case 0xC: ea = (uint16_t)(pc + 1 + (int8_t)FETCH(pc)); bytes = 1; cyc = 1; DV(1); break;
    case 0xD: ea = (uint16_t)(pc + 2 + (FETCH(pc) << 8 | FETCH(pc + 1))); bytes = 2; cyc = 5; DV(4); break;
    case 0xF: ea = (uint16_t)(FETCH(pc) << 8 | FETCH(pc + 1)); bytes = 2; cyc = 2; DV(1); break;
    default:  ea = 0; cyc = -1; break;  // (undefined, MAME's: $0000, the postbyte's cycle only)
    }
    if (pb & 0x10) {                    // indirect
        ea = RD16(ea);
        cyc += 3;
        DV(1);
    }
    return ea | bytes << 16 | (uint32_t)cyc << 24;
}
#define EA_IDX()  (idx = M_FN(x_indexed)(c, pc), pc = (uint16_t)(pc + 1 + (idx >> 16 & 3)), \
                   cyc += (int32_t)idx >> 24, (uint16_t)idx)
#define EA_DIR()  (ea_ = (uint16_t)(c->dp << 8 | IMM8()), DV(1), ea_)
#define EA_EXT()  (ea_ = IMM16(), DV(1), ea_)

// ---- TFR / EXG (MAME's read_tfr_exg_816_register, read_exg_168_register) ------------
static inline uint16_t M_FN(x_getreg)(m6809_t *c, unsigned r, bool cc_twice) {
    switch (r & 15) {
    case 0: return D();
    case 1: return c->x;
    case 2: return c->y;
    case 3: return c->u;
    case 4: return c->s;
    case 5: return c->pc;
    case 8: return (uint16_t)(0xFF00 | c->a);
    case 9: return (uint16_t)(0xFF00 | c->b);
    case 10: return (uint16_t)((cc_twice ? c->cc << 8 : 0xFF00) | c->cc);
    case 11: return (uint16_t)((cc_twice ? c->dp << 8 : 0xFF00) | c->dp);
    default: return 0xFFFF;
    }
}
static inline void M_FN(x_setreg)(m6809_t *c, unsigned r, uint16_t v) {
    switch (r & 15) {
    case 0: SET_D(v); break;
    case 1: c->x = v; break;
    case 2: c->y = v; break;
    case 3: c->u = v; break;
    case 4: c->s = v; break;
    case 5: c->pc = v; break;
    case 8: c->a = (uint8_t)v; break;
    case 9: c->b = (uint8_t)v; break;
    case 10: c->cc = (uint8_t)v; break;
    case 11: c->dp = (uint8_t)v; break;
    default: break;
    }
}

// ---- stacking --------------------------------------------------------------------
static inline void M_FN(x_push_all)(m6809_t *c) {
    PUSHS16(c->pc); PUSHS16(c->u); PUSHS16(c->y); PUSHS16(c->x);
    PUSHS8(c->dp); PUSHS8(c->b); PUSHS8(c->a); PUSHS8(c->cc);
}

static int M_FN(x_psh)(m6809_t *c, uint8_t pb, bool s_stack) {
    int n = 0;
    if (s_stack) {
        if (pb & 0x80) { PUSHS16(c->pc); n += 2; }
        if (pb & 0x40) { PUSHS16(c->u); n += 2; }
        if (pb & 0x20) { PUSHS16(c->y); n += 2; }
        if (pb & 0x10) { PUSHS16(c->x); n += 2; }
        if (pb & 0x08) { PUSHS8(c->dp); n++; }
        if (pb & 0x04) { PUSHS8(c->b); n++; }
        if (pb & 0x02) { PUSHS8(c->a); n++; }
        if (pb & 0x01) { PUSHS8(c->cc); n++; }
    } else {
        if (pb & 0x80) { PUSHU16(c->pc); n += 2; }
        if (pb & 0x40) { PUSHU16(c->s); n += 2; }
        if (pb & 0x20) { PUSHU16(c->y); n += 2; }
        if (pb & 0x10) { PUSHU16(c->x); n += 2; }
        if (pb & 0x08) { PUSHU8(c->dp); n++; }
        if (pb & 0x04) { PUSHU8(c->b); n++; }
        if (pb & 0x02) { PUSHU8(c->a); n++; }
        if (pb & 0x01) { PUSHU8(c->cc); n++; }
    }
    return n;
}
static int M_FN(x_pul)(m6809_t *c, uint8_t pb, bool s_stack) {
    int n = 0;
    if (s_stack) {
        if (pb & 0x01) { c->cc = PULLS8(); n++; }
        if (pb & 0x02) { c->a = PULLS8(); n++; }
        if (pb & 0x04) { c->b = PULLS8(); n++; }
        if (pb & 0x08) { c->dp = PULLS8(); n++; }
        if (pb & 0x10) { c->x = PULLS16(); n += 2; }
        if (pb & 0x20) { c->y = PULLS16(); n += 2; }
        if (pb & 0x40) { c->u = PULLS16(); n += 2; }
        if (pb & 0x80) { c->pc = PULLS16(); n += 2; }
    } else {
        if (pb & 0x01) { c->cc = PULLU8(); n++; }
        if (pb & 0x02) { c->a = PULLU8(); n++; }
        if (pb & 0x04) { c->b = PULLU8(); n++; }
        if (pb & 0x08) { c->dp = PULLU8(); n++; }
        if (pb & 0x10) { c->x = PULLU16(); n += 2; }
        if (pb & 0x20) { c->y = PULLU16(); n += 2; }
        if (pb & 0x40) { c->s = PULLU16(); n += 2; }
        if (pb & 0x80) { c->pc = PULLU16(); n += 2; }
    }
    return n;
}

// the RESET pin (MAME's device_reset, then the vector's 4 cycles)
void M_FN(reset)(m6809_t *c) {
    c->nmi = 0;
    c->wait = 0;
    c->lds = 0;
    c->dp = 0;
    c->cc |= M6809_CC_I | M6809_CC_F;
    c->pc = RD16(0xFFFE);
    c->end += 4;
}

// Between instructions: an interrupt to take, or SYNC's or CWAI's wait, or the
// free run. Returns the cycles used, 0 for none (run an instruction), or -1:
// waiting for a line (the rest of the timeslice goes by).
static int M_FN(x_interrupts)(m6809_t *c) {
    const uint16_t pc = c->pc;          // (the dummy cycles' address)
    (void)pc;
    if (c->wait == 1) {                 // SYNC: any line ends it, masked or not
        if (!(c->nmi | c->firq | c->irq)) return -1;
        c->wait = 0;
        return 1;                       // its last cycle; then the lines again
    }
    const bool cwai = c->wait == 2;     // (CWAI stacked the state already)
    uint16_t vec;
    int n;
    if (c->nmi) {
        c->nmi = 0;
        if (!cwai) { DA(0); DA(0); DV(1); c->cc |= M6809_CC_E; M_FN(x_push_all)(c); }
        c->cc |= M6809_CC_I | M6809_CC_F;
        M6809_IACK(M6809_LINE_NMI);
        vec = 0xFFFC;
        n = 19;
    } else if (c->firq && !(c->cc & M6809_CC_F)) {
        if (!cwai) { DA(0); DA(0); DV(1); c->cc &= (uint8_t)~M6809_CC_E; PUSHS16(c->pc); PUSHS8(c->cc); }
        c->cc |= M6809_CC_I | M6809_CC_F;
        M6809_IACK(M6809_LINE_FIRQ);
        vec = 0xFFF6;
        n = 10;
    } else if (c->irq && !(c->cc & M6809_CC_I)) {
        if (!cwai) { DA(0); DA(0); DV(1); c->cc |= M6809_CC_E; M_FN(x_push_all)(c); }
        c->cc |= M6809_CC_I;
        M6809_IACK(M6809_LINE_IRQ);
        vec = 0xFFF8;
        n = 19;
    } else if (c->wait == 3) {          // free run ($14, $15, $CD): an address a cycle until reset
        (void)RD(c->pc);
        c->pc++;
        return 1;
    } else {
        return cwai ? -1 : 0;
    }
    if (cwai) {                         // only the vector's cycles are left
        c->wait = 0;
        n = 4;
    }
    DV(1);
    c->pc = RD16(vec);
    DV(1);
    return n;
}

// the 8-bit operations of $80-$FF, in their four addressing modes
#define ALU8(opc, EXPR) \
    case opc:        m = IMM8(); EXPR; cyc += 2; break; \
    case opc + 0x10: ea = EA_DIR(); m = RD(ea); EXPR; cyc += 4; break; \
    case opc + 0x20: ea = EA_IDX(); m = RD(ea); EXPR; cyc += 4; break; \
    case opc + 0x30: ea = EA_EXT(); m = RD(ea); EXPR; cyc += 5; break;
#define ALU16(opc, EXPR, ci, cd, cx, ce) \
    case opc:        w = IMM16(); EXPR; cyc += ci; break; \
    case opc + 0x10: ea = EA_DIR(); w = RD16(ea); EXPR; cyc += cd; break; \
    case opc + 0x20: ea = EA_IDX(); w = RD16(ea); EXPR; cyc += cx; break; \
    case opc + 0x30: ea = EA_EXT(); w = RD16(ea); EXPR; cyc += ce; break;
// (wofs: the write's cycle in the instruction; a handler that keeps time adds it)
#define ST8(opc, REG) \
    case opc + 0x10: ea = EA_DIR(); M_FN(x_nz8)(c, REG); c->wofs = 3; WR(ea, REG); cyc += 4; break; \
    case opc + 0x20: ea = EA_IDX(); M_FN(x_nz8)(c, REG); c->wofs = (uint8_t)(cyc + 3); WR(ea, REG); cyc += 4; break; \
    case opc + 0x30: ea = EA_EXT(); M_FN(x_nz8)(c, REG); c->wofs = 4; WR(ea, REG); cyc += 5; break;
#define ST16(opc, VAL, cd, cx, ce) \
    case opc + 0x10: ea = EA_DIR(); w = (VAL); M_FN(x_nz16)(c, w); WR16(ea, w); cyc += cd; break; \
    case opc + 0x20: ea = EA_IDX(); w = (VAL); M_FN(x_nz16)(c, w); WR16(ea, w); cyc += cx; break; \
    case opc + 0x30: ea = EA_EXT(); w = (VAL); M_FN(x_nz16)(c, w); WR16(ea, w); cyc += ce; break;
// (undocumented) a store to an immediate: 8 bits reads the byte and stores
// nothing; 16 bits reads the first and writes the register's low byte over the
// second. Their flags are MAME's.
#define XST8(REG, cy)  m = IMM8(); (void)m; M_FN(x_nz8)(c, REG); cyc += cy
#define XST16(VAL, cy) w = (VAL); (void)FETCH(pc); pc++; WR(pc, w); pc++; M_FN(x_nz16)(c, w); cyc += cy
// (undocumented) SWI2 that doesn't set E ($10 $3E, vector $FFF4) and its like on
// page 3 ($11 $3E: the FIRQ vector): the whole state, no masks
#define XSWI(vec) c->pc = pc; DA(0); DV(1); M_FN(x_push_all)(c); DV(1); pc = RD16(vec); DV(1); cyc += 20
#define BRANCH(cond) do { int8_t o_ = (int8_t)IMM8(); DV(1); if (cond) pc = (uint16_t)(pc + o_); cyc += 3; } while (0)
#define LBRANCH(cond) do { uint16_t o_ = IMM16(); DV(1); cyc += 5; if (cond) { pc = (uint16_t)(pc + o_); cyc += 1; DV(1); } } while (0)
#define F_C (c->cc & M6809_CC_C)
#define F_V (c->cc & M6809_CC_V)
#define F_Z (c->cc & M6809_CC_Z)
#define F_N (c->cc & M6809_CC_N)
#define F_NXV (((c->cc >> 3) ^ (c->cc >> 1)) & 1)

static inline bool M_FN(x_cond)(m6809_t *c, unsigned n) {
    switch (n & 15) {
    case 0x0: return true;
    case 0x1: return false;
    case 0x2: return !(F_C || F_Z);
    case 0x3: return F_C || F_Z;
    case 0x4: return !F_C;
    case 0x5: return F_C;
    case 0x6: return !F_Z;
    case 0x7: return F_Z;
    case 0x8: return !F_V;
    case 0x9: return F_V;
    case 0xA: return !F_N;
    case 0xB: return F_N;
    case 0xC: return !F_NXV;
    case 0xD: return F_NXV;
    case 0xE: return !(F_NXV || F_Z);
    default:  return F_NXV || F_Z;
    }
}

// Under M6809_SLOW_STEP a prefix's next opcode can be on a slow page, which the
// fast bus can't fetch: the prefix then ends its instruction there (see the top).
#ifdef M6809_SLOW_STEP
#define REBASED() (__builtin_expect((fbase = M6809_FETCH_BASE(pc)) != 0, 1))
#else
#define REBASED() ((fbase = M6809_FETCH_BASE(pc)), true)
#endif

// Runs a timeslice: instructions while cycles are left (the last may overrun);
// returns the cycles run.
int M6809_HOT M_FN(run)(m6809_t *c, int cycles) {
    c->end = m6809_now(c) + (uint64_t)(int64_t)cycles;
    c->icount = cycles;
    c->slice = cycles;
    uint16_t pc = c->pc;
    while (c->icount > 0) {
        if (c->pending) {
            c->pc = pc;
            int took = M_FN(x_interrupts)(c);
            pc = c->pc;
            if (took > 0) { c->icount -= took; continue; }
            if (took < 0) {             // waiting: the rest of the slice goes by
                c->icount = 0;
                break;
            }
        }
        bool recheck = false;           // the masks changed: the lines again
        do {
        int cyc = 0;
        uint16_t ea, ea_, w;
        uint32_t idx;
        uint8_t m;
        unsigned op, op2;
#if defined(M6809_TRACE) && !defined(M6809_NO_TRACE)
        c->pc = pc;
        M6809_TRACE(c);                 // (tests: the registers before each instruction)
#endif
        uintptr_t fbase = M6809_FETCH_BASE(pc);
        (void)fbase;
#ifdef M6809_SLOW_STEP
        if (__builtin_expect(!fbase, 0)) {
            c->pc = pc;
            M6809_SLOW_STEP(c);
            pc = c->pc;
            recheck = true;
            continue;
        }
#endif
        op = IMM8();
    page1:
        switch (op) {
        // ---- $00-$0F direct, $60-$6F indexed, $70-$7F extended: read-modify-write
        case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0F:
            ea = EA_DIR(); m = M_FN(x_rmw)(c, op, RD(ea)); DA(0); WR(ea, m); cyc += 6; break;
        case 0x0D: ea = EA_DIR(); M_FN(x_nz8)(c, RD(ea)); DV(2); cyc += 6; break;
        case 0x0E: ea = EA_DIR(); pc = ea; cyc += 3; break;
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
        case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6F:
            ea = EA_IDX(); m = M_FN(x_rmw)(c, op, RD(ea)); DA(0); WR(ea, m); cyc += 6; break;
        case 0x6D: ea = EA_IDX(); M_FN(x_nz8)(c, RD(ea)); DV(2); cyc += 6; break;
        case 0x6E: ea = EA_IDX(); pc = ea; cyc += 3; break;
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7F:
            ea = EA_EXT(); m = M_FN(x_rmw)(c, op, RD(ea)); DA(0); WR(ea, m); cyc += 7; break;
        case 0x7D: ea = EA_EXT(); M_FN(x_nz8)(c, RD(ea)); DV(2); cyc += 7; break;
        case 0x7E: ea = EA_EXT(); pc = ea; cyc += 4; break;
        // ---- $40-$5F: the same on A and B ($4E, $5E: CLR that keeps C)
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4E: case 0x4F:
            c->a = M_FN(x_rmw)(c, op, c->a); DA(0); cyc += 2; break;
        case 0x4D: M_FN(x_nz8)(c, c->a); DV(1); cyc += 2; break;
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5E: case 0x5F:
            c->b = M_FN(x_rmw)(c, op, c->b); DA(0); cyc += 2; break;
        case 0x5D: M_FN(x_nz8)(c, c->b); DV(1); cyc += 2; break;

        // ---- $10-$1F
        case 0x12: case 0x1B: DA(0); cyc += 2; break;              // NOP ($1B: undocumented)
        case 0x13: DA(0); c->wait = 1; recheck = true; cyc += 2; break;     // SYNC (its last cycle when it ends)
        case 0x14: case 0x15: case 0xCD:                           // (undocumented: free run until reset)
            c->wait = 3; recheck = true; cyc += 1; break;
        case 0x16: { uint16_t o = IMM16(); DV(2); pc = (uint16_t)(pc + o); cyc += 5; break; }      // LBRA
        case 0x17: { uint16_t o = IMM16(); DV(4); PUSHS16(pc); pc = (uint16_t)(pc + o); cyc += 9; break; }    // LBSR
        case 0x18:                                                 // (undocumented: CC from CC and the next byte, which stays)
            m = FETCH(pc);
            c->cc = (uint8_t)(((c->cc & m) << 1) | ((c->cc & M6809_CC_Z) >> 1));
            recheck = true;
            cyc += 2;
            break;
        case 0x19: {                                               // DAA
            unsigned a = c->a, cf = 0;
            if ((a & 0x0F) > 9 || (c->cc & M6809_CC_H)) cf |= 0x06;
            if (a > 0x99 || (c->cc & M6809_CC_C) || ((a & 0xF0) > 0x80 && (a & 0x0F) > 9)) cf |= 0x60;
            unsigned r = a + cf;
            uint8_t cc = c->cc & ~(M6809_CC_N | M6809_CC_Z | M6809_CC_V);
            if (r & 0x100 || (cf & 0x60)) cc |= M6809_CC_C;
            c->a = (uint8_t)r;
            c->cc = (uint8_t)(cc | m6809_nz_[c->a]);
            DA(0);
            cyc += 2;
            break;
        }
        case 0x1A: c->cc |= IMM8(); DA(0); cyc += 3; break;        // ORCC
        case 0x1C: c->cc &= IMM8(); DA(0); recheck = true; cyc += 3; break; // ANDCC
        case 0x38: c->cc &= IMM8(); DA(0); recheck = true; cyc += 4; break; // (undocumented: ANDCC a cycle longer)
        case 0x1D:                                                 // SEX (V stays, as MAME has it)
            c->a = (c->b & 0x80) ? 0xFF : 0;
            c->cc = (uint8_t)((c->cc & ~(M6809_CC_N | M6809_CC_Z)) | (c->b & 0x80 ? M6809_CC_N : 0) | (c->b ? 0 : M6809_CC_Z));
            DA(0);
            cyc += 2;
            break;
        case 0x1E: {                                               // EXG
            uint8_t pb = IMM8();
            c->pc = pc;
            const bool eight_first = pb & 0x80;
            uint16_t t1 = M_FN(x_getreg)(c, pb >> 4, eight_first), t2 = M_FN(x_getreg)(c, pb, eight_first);
            M_FN(x_setreg)(c, pb, t1);                             // (the second first, as MAME)
            M_FN(x_setreg)(c, pb >> 4, t2);
            pc = c->pc;
            DV(6);
            recheck = true;
            cyc += 8;
            break;
        }
        case 0x1F: {                                               // TFR
            uint8_t pb = IMM8();
            c->pc = pc;
            M_FN(x_setreg)(c, pb, M_FN(x_getreg)(c, pb >> 4, true));
            if ((pb & 15) == 4) c->lds = 1;
            pc = c->pc;
            DV(4);
            recheck = true;
            cyc += 6;
            break;
        }
        // ---- $20-$2F: short branches
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
        case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
            BRANCH(M_FN(x_cond)(c, op)); break;
        // ---- $30-$3F
        case 0x30: c->x = EA_IDX(); c->cc = (uint8_t)((c->cc & ~M6809_CC_Z) | (c->x ? 0 : M6809_CC_Z)); DV(1); cyc += 4; break;
        case 0x31: c->y = EA_IDX(); c->cc = (uint8_t)((c->cc & ~M6809_CC_Z) | (c->y ? 0 : M6809_CC_Z)); DV(1); cyc += 4; break;
        case 0x32: c->s = EA_IDX(); c->lds = 1; DV(1); cyc += 4; break;
        case 0x33: c->u = EA_IDX(); DV(1); cyc += 4; break;
        case 0x34: m = IMM8(); DV(2); DM(c->s); c->pc = pc; cyc += 5 + M_FN(x_psh)(c, m, true); break;
        case 0x35: m = IMM8(); DV(2); c->pc = pc; cyc += 5 + M_FN(x_pul)(c, m, true); pc = c->pc; DM(c->s); recheck = true; break;
        case 0x36: m = IMM8(); DV(2); DM(c->u); c->pc = pc; cyc += 5 + M_FN(x_psh)(c, m, false); break;
        case 0x37: m = IMM8(); DV(2); c->pc = pc; cyc += 5 + M_FN(x_pul)(c, m, false); pc = c->pc; DM(c->u); recheck = true; break;
        case 0x39: DA(0); pc = PULLS16(); DM(c->s); cyc += 5; break;        // RTS
        case 0x3A: c->x = (uint16_t)(c->x + c->b); DA(0); DV(1); cyc += 3; break;   // ABX
        case 0x3B:                                                 // RTI
            DA(0);
            c->cc = PULLS8();
            if (c->cc & M6809_CC_E) {
                c->a = PULLS8(); c->b = PULLS8(); c->dp = PULLS8();
                c->x = PULLS16(); c->y = PULLS16(); c->u = PULLS16();
                cyc += 9;
            }
            pc = PULLS16();
            DM(c->s);
            recheck = true;
            cyc += 6;
            break;
        case 0x3C:                                                 // CWAI (the vector's 4 cycles when it ends)
            c->cc &= IMM8();
            DA(0); DV(1);
            c->cc |= M6809_CC_E;
            c->pc = pc;
            M_FN(x_push_all)(c);
            c->wait = 2;
            recheck = true;
            cyc += 16;
            break;
        case 0x3D: {                                               // MUL
            uint16_t r = (uint16_t)(c->a * c->b);
            SET_D(r);
            c->cc = (uint8_t)((c->cc & ~(M6809_CC_Z | M6809_CC_C)) | (r ? 0 : M6809_CC_Z) | (r & 0x80 ? M6809_CC_C : 0));
            DA(0); DV(9);
            cyc += 11;
            break;
        }
        case 0x3E:                                                 // (undocumented: the reset vector, as SWI but no E, no masks)
            c->pc = pc;
            DA(0); DV(1);
            M_FN(x_push_all)(c);
            DV(1);
            pc = RD16(0xFFFE);
            DV(1);
            cyc += 19;
            break;
        case 0x3F:                                                 // SWI
            c->cc |= M6809_CC_E;
            c->pc = pc;
            DA(0); DV(1);
            M_FN(x_push_all)(c);
            c->cc |= M6809_CC_I | M6809_CC_F;
            DV(1);
            pc = RD16(0xFFFA);
            DV(1);
            cyc += 19;
            break;

        // ---- $80-$FF: A and B
        ALU8(0x80, c->a = M_FN(x_sub8)(c, c->a, m, 0))
        ALU8(0x81, (void)M_FN(x_sub8)(c, c->a, m, 0))
        ALU8(0x82, c->a = M_FN(x_sub8)(c, c->a, m, F_C))
        ALU16(0x83, SET_D(M_FN(x_sub16)(c, D(), w)); DA(0), 4, 6, 6, 7)
        ALU8(0x84, c->a &= m; M_FN(x_nz8)(c, c->a))
        ALU8(0x85, M_FN(x_nz8)(c, c->a & m))
        ALU8(0x86, c->a = m; M_FN(x_nz8)(c, m))
        case 0x87: XST8(c->a, 2); break;
        ST8(0x87, c->a)
        ALU8(0x88, c->a ^= m; M_FN(x_nz8)(c, c->a))
        ALU8(0x89, c->a = M_FN(x_add8)(c, c->a, m, F_C))
        ALU8(0x8A, c->a |= m; M_FN(x_nz8)(c, c->a))
        ALU8(0x8B, c->a = M_FN(x_add8)(c, c->a, m, 0))
        ALU16(0x8C, (void)M_FN(x_sub16)(c, c->x, w); DA(0), 4, 6, 6, 7)
        case 0x8D: { int8_t o = (int8_t)IMM8(); DV(3); PUSHS16(pc); pc = (uint16_t)(pc + o); cyc += 7; break; }    // BSR
        case 0x9D: ea = EA_DIR(); DA(0); DV(1); PUSHS16(pc); pc = ea; cyc += 7; break;              // JSR
        case 0xAD: ea = EA_IDX(); DA(0); DV(1); PUSHS16(pc); pc = ea; cyc += 7; break;
        case 0xBD: ea = EA_EXT(); DA(0); DV(1); PUSHS16(pc); pc = ea; cyc += 8; break;
        ALU16(0x8E, c->x = w; M_FN(x_nz16)(c, w), 3, 5, 5, 6)
        case 0x8F: XST16(c->x, 3); break;
        ST16(0x8F, c->x, 5, 5, 6)

        ALU8(0xC0, c->b = M_FN(x_sub8)(c, c->b, m, 0))
        ALU8(0xC1, (void)M_FN(x_sub8)(c, c->b, m, 0))
        ALU8(0xC2, c->b = M_FN(x_sub8)(c, c->b, m, F_C))
        ALU16(0xC3, SET_D(M_FN(x_add16)(c, D(), w)); DA(0), 4, 6, 6, 7)
        ALU8(0xC4, c->b &= m; M_FN(x_nz8)(c, c->b))
        ALU8(0xC5, M_FN(x_nz8)(c, c->b & m))
        ALU8(0xC6, c->b = m; M_FN(x_nz8)(c, m))
        case 0xC7: XST8(c->b, 2); break;
        ST8(0xC7, c->b)
        ALU8(0xC8, c->b ^= m; M_FN(x_nz8)(c, c->b))
        ALU8(0xC9, c->b = M_FN(x_add8)(c, c->b, m, F_C))
        ALU8(0xCA, c->b |= m; M_FN(x_nz8)(c, c->b))
        ALU8(0xCB, c->b = M_FN(x_add8)(c, c->b, m, 0))
        ALU16(0xCC, SET_D(w); M_FN(x_nz16)(c, w), 3, 5, 5, 6)
        ST16(0xCD, D(), 5, 5, 6)
        ALU16(0xCE, c->u = w; M_FN(x_nz16)(c, w), 3, 5, 5, 6)
        case 0xCF: XST16(c->u, 3); break;
        ST16(0xCF, c->u, 5, 5, 6)

        // ---- page 2 (a further $10 or $11 keeps to page 2; anything not on it:
        // the prefix and that byte go by and a new opcode is fetched)
        case 0x10:
        page2:
            op2 = IMM8();
            switch (op2) {
            case 0x10: case 0x11:                              // (the slow page: this prefix starts the next one)
                if (!REBASED()) { pc--; cyc += 1; break; }
                cyc += 1;
                goto page2;
            case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
            case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
                LBRANCH(M_FN(x_cond)(c, op2)); break;
            case 0x3E: XSWI(0xFFF4); break;                        // (undocumented)
            case 0x3F:                                             // SWI2
                c->cc |= M6809_CC_E;
                c->pc = pc;
                DA(0); DV(1);
                M_FN(x_push_all)(c);
                DV(1);
                pc = RD16(0xFFF4);
                DV(1);
                cyc += 20;
                break;
            ALU16(0x83, (void)M_FN(x_sub16)(c, D(), w); DA(0), 5, 7, 7, 8)
            case 0x87: XST8(c->a, 3); break;
            ALU16(0x8C, (void)M_FN(x_sub16)(c, c->y, w); DA(0), 5, 7, 7, 8)
            ALU16(0x8E, c->y = w; M_FN(x_nz16)(c, w), 4, 6, 6, 7)
            case 0x8F: XST16(c->y, 4); break;
            ST16(0x8F, c->y, 6, 6, 7)
            ALU16(0xC3, (void)M_FN(x_add16)(c, D(), w); DA(0), 5, 7, 7, 8)     // (undocumented: ADDD, flags only)
            case 0xC7: XST8(c->b, 3); break;
            ALU16(0xCE, c->s = w; c->lds = 1; M_FN(x_nz16)(c, w), 4, 6, 6, 7)
            case 0xCF: XST16(c->s, 4); break;
            ST16(0xCF, c->s, 6, 6, 7)
            default:
                cyc += 2;
                if (!REBASED()) break;
                op = IMM8();
                goto page1;
            }
            break;
        // ---- page 3 (the same, kept to page 3)
        case 0x11:
        page3:
            op2 = IMM8();
            switch (op2) {
            case 0x10: case 0x11:
                if (!REBASED()) { pc--; cyc += 1; break; }
                cyc += 1;
                goto page3;
            case 0x3E: XSWI(0xFFF6); break;                        // (undocumented)
            case 0x3F:                                             // SWI3
                c->cc |= M6809_CC_E;
                c->pc = pc;
                DA(0); DV(1);
                M_FN(x_push_all)(c);
                DV(1);
                pc = RD16(0xFFF2);
                DV(1);
                cyc += 20;
                break;
            ALU16(0x83, (void)M_FN(x_sub16)(c, c->u, w); DA(0), 5, 7, 7, 8)
            case 0x87: XST8(c->a, 3); break;
            ALU16(0x8C, (void)M_FN(x_sub16)(c, c->s, w); DA(0), 5, 7, 7, 8)
            case 0x8F: XST16(c->x, 4); break;
            case 0xC3: case 0xD3: case 0xE3: case 0xF3:            // (undocumented: ADDU, flags only)
                switch (op2) {
                case 0xC3: w = IMM16(); cyc += 5; break;
                case 0xD3: ea = EA_DIR(); w = RD16(ea); cyc += 7; break;
                case 0xE3: ea = EA_IDX(); w = RD16(ea); cyc += 7; break;
                default:   ea = EA_EXT(); w = RD16(ea); cyc += 8; break;
                }
                (void)M_FN(x_add16)(c, c->u, w);
                DA(0);
                break;
            case 0xC7: XST8(c->b, 3); break;
            case 0xCF: XST16(c->u, 4); break;
            default:
                cyc += 2;
                if (!REBASED()) break;
                op = IMM8();
                goto page1;
            }
            break;
        }
        c->icount -= cyc;
        } while (c->icount > 0 && !recheck);
    }
    c->pc = pc;
    return c->slice - c->icount;
}

// One instruction (or the interrupt entry that comes first) inside the running
// timeslice: its cycles come off the slice's count, and a handler's
// m6809_abort ends the slice after it, as if the run loop had run it. Returns
// its cycles. (M6809_SLOW_STEP's: the slow copy's step, called by the fast one.)
int M_FN(step)(m6809_t *c) {
    const uint64_t end = c->end;
    const int32_t left = c->icount, slice = c->slice;
    const uint64_t t0 = end - (uint64_t)(int64_t)left;
    M_FN(run)(c, 1);
    const int cyc = (int)(m6809_now(c) - t0);
    if (c->slice != 1) {                // aborted inside: the slice ends after it
        c->end = t0;
        c->slice = slice - left;
        c->icount = -cyc;
    } else {
        c->end = end;
        c->slice = slice;
        c->icount = left - cyc;
    }
    return cyc;
}

#undef RD
#undef DM
#undef DA
#undef DV
#undef M6809_DUMMY
#undef M6809_DUMMY_VMA
#undef M6809_SLOW_STEP
#undef M6809_NO_TRACE
#undef M6809_IACK
#undef FETCH
#undef M6809_FETCH
#undef M6809_FETCH_BASE
#undef WR
#undef RD16
#undef WR16
#undef IMM8
#undef IMM16
#undef D
#undef SET_D
#undef PUSHS8
#undef PUSHS16
#undef PULLS8
#undef PUSHU8
#undef PUSHU16
#undef PULLU8
#undef PULLS16
#undef PULLU16
#undef EA_IDX
#undef EA_DIR
#undef EA_EXT
#undef ALU8
#undef ALU16
#undef ST8
#undef ST16
#undef XST8
#undef XST16
#undef XSWI
#undef BRANCH
#undef LBRANCH
#undef REBASED
#undef F_C
#undef F_V
#undef F_Z
#undef F_N
#undef F_NXV
#undef M_FN
#undef M6809_NAME
#undef M6809_RD
#undef M6809_WR
#undef M6809_HOT
