// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Parrot Cart contributors
//
// z80lite.c -- see z80lite.h. Written from the Z80's documentation (Zilog's
// user manual and "The Undocumented Z80 Documented", Sean Young): the
// opcodes decoded by their fields, one switch for the main table, CB, ED and
// the DD and FD prefixes, which put IX or IY (and their halves) where HL
// is, (IX+d) where (HL) is. It came from the Neo Geo Pocket Color plugin's
// sound CPU (ngpz80.c), with its state moved into a z80lite_t and the
// Pocket's memory map replaced by windows and hooks.
#include "z80lite.h"

#include <stddef.h>
#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define HOT __attribute__((section(".time_critical.z80lite")))
#else
#define HOT
#endif
#define ALWAYS __attribute__((always_inline)) inline
#define NOINLINE __attribute__((noinline))

enum { CF = 1, NF = 2, PF = 4, VF = 4, XF = 8, HF = 16, YF = 32, ZF = 64, SF = 128 };

// the flags of a result, shared by every CPU (z80lite_init fills them)
static uint8_t SZ[256], SZP[256], SZHV_INC[256], SZHV_DEC[256];

// (every function here has the CPU as z)
#define A  z->af.b.h
#define F  z->af.b.l
#define B  z->bc.b.h
#define C  z->bc.b.l
#define D  z->de.b.h
#define E  z->de.b.l
#define H  z->hl.b.h
#define L  z->hl.b.l
#define BC z->bc.w
#define DE z->de.w
#define HL z->hl.w
#define SP z->sp.w
#define PC z->pc.w
#define WZ z->wz.w
#define Q_NONE (YF | XF)                    // z->q: the last instruction left the flags alone

// ---- memory and I/O ---------------------------------------------------------------------------
// z->writes counts the writes that changed something (any to a hook), for
// the idle loops (loop_check).
static NOINLINE HOT uint8_t rd_bus(z80lite_t *z, uint16_t a) { return z->rd ? z->rd(z, a) : 0xFF; }
static NOINLINE HOT void wr_bus(z80lite_t *z, uint16_t a, uint8_t v) {
    z->writes++;
    if (z->wr) z->wr(z, a, v);
}
static NOINLINE HOT uint8_t in_port(z80lite_t *z, uint16_t port) { return z->in ? z->in(z, port) : 0xFF; }
static NOINLINE HOT void out_port(z80lite_t *z, uint16_t port, uint8_t v) {
    z->writes++;
    if (z->out) z->out(z, port, v);
}

// a data read: the RAM window, the ROM window, the hook
static ALWAYS uint8_t rd(z80lite_t *z, uint16_t a) {
    uint32_t o = (uint32_t)a - z->ram_base;
    if (__builtin_expect(o < z->ram_size, 1)) return z->ram[o];
    o = (uint32_t)a - z->rom_base;
    if (__builtin_expect(o < z->rom_size, 1)) return z->rom[o];
    return rd_bus(z, a);
}
// an opcode or operand fetch: the ROM window first (a board whose code
// runs from RAM points the ROM window at it too)
static ALWAYS uint8_t rd_code(z80lite_t *z, uint16_t a) {
    uint32_t o = (uint32_t)a - z->rom_base;
    if (__builtin_expect(o < z->rom_size, 1)) return z->rom[o];
    o = (uint32_t)a - z->ram_base;
    if (__builtin_expect(o < z->ram_size, 1)) return z->ram[o];
    return rd_bus(z, a);
}
static ALWAYS void wr(z80lite_t *z, uint16_t a, uint8_t v) {
    const uint32_t o = (uint32_t)a - z->ram_base;
    if (__builtin_expect(o < z->ram_size, 1)) {
        uint8_t *const p = z->ram + o;
        if (*p != v) {
            *p = v;
            z->writes++;
        }
    } else {
        wr_bus(z, a, v);
    }
}

// ---- idle loops (idle_skip) ---------------------------------------------------------------------
// A loop that comes back to the same jump with the same registers, and
// nothing written in between, would do the same for the rest of the slice
// if nothing outside the Z80 changes meanwhile: a sound driver waiting for
// its next interrupt or command. z->idle says so, and z80lite_run lets the
// rest of the time pass at once.
static inline uint16_t irq_state(const z80lite_t *z) { return (uint16_t)(z->iff1 | z->iff2 << 1 | z->im << 2 | z->i << 8); }

static NOINLINE HOT void loop_check(z80lite_t *z) {
    uint16_t *const r = z->idle_r;
    if (PC == z->idle_pc && z->writes == z->idle_w) {
        // (the ones a loop changes most often first)
        if (z->af.w == r[0] && BC == r[1] && DE == r[2] && HL == r[3] && z->ix.w == r[4] && z->iy.w == r[5] &&
            SP == r[6] && z->af2.w == r[7] && z->bc2.w == r[8] && z->de2.w == r[9] && z->hl2.w == r[10] &&
            irq_state(z) == r[11]) {
            z->idle = 1;
            z->attn = 1;
            return;
        }
        // Not the same: the snapshot is kept for 7 times more. A match with
        // an older one, with nothing written since, says as much (the Z80
        // came round to where it was), and a loop that counts is copied
        // less often.
        if (++z->idle_miss & 7) return;
    }
    z->idle_miss = 0;
    z->idle_pc = PC;
    z->idle_w = z->writes;
    r[0] = z->af.w;
    r[1] = BC;
    r[2] = DE;
    r[3] = HL;
    r[4] = z->ix.w;
    r[5] = z->iy.w;
    r[6] = SP;
    r[7] = z->af2.w;
    r[8] = z->bc2.w;
    r[9] = z->de2.w;
    r[10] = z->hl2.w;
    r[11] = irq_state(z);
}
// a jump taken back to `from` or before it
#define BACK(from) do { if (PC <= (from) && z->idle_skip) loop_check(z); } while (0)

static ALWAYS uint16_t rd16(z80lite_t *z, uint16_t a) { return (uint16_t)(rd(z, a) | rd(z, (uint16_t)(a + 1)) << 8); }
static ALWAYS void wr16(z80lite_t *z, uint16_t a, uint16_t v) {
    wr(z, a, (uint8_t)v);
    wr(z, (uint16_t)(a + 1), (uint8_t)(v >> 8));
}
static ALWAYS uint8_t fetch(z80lite_t *z) { return rd_code(z, PC++); }
static ALWAYS uint16_t fetch16(z80lite_t *z) {
    const uint16_t v = (uint16_t)(rd_code(z, PC) | rd_code(z, (uint16_t)(PC + 1)) << 8);
    PC += 2;
    return v;
}
static ALWAYS void push(z80lite_t *z, uint16_t v) {     // high byte first, as the Z80 does
    wr(z, --SP, (uint8_t)(v >> 8));
    wr(z, --SP, (uint8_t)v);
}
static ALWAYS uint16_t pop(z80lite_t *z) {
    const uint16_t v = rd16(z, SP);
    SP += 2;
    return v;
}

// ---- the arithmetic ---------------------------------------------------------------------------
static ALWAYS void add8(z80lite_t *z, uint8_t v) {
    const unsigned r = A + v;
    F = (uint8_t)(SZ[r & 0xFF] | (r >> 8 & CF) | ((A ^ r ^ v) & HF) | (((v ^ A ^ 0x80) & (v ^ r) & 0x80) >> 5));
    A = (uint8_t)r;
}
static ALWAYS void adc8(z80lite_t *z, uint8_t v) {
    const unsigned r = A + v + (F & CF);
    F = (uint8_t)(SZ[r & 0xFF] | (r >> 8 & CF) | ((A ^ r ^ v) & HF) | (((v ^ A ^ 0x80) & (v ^ r) & 0x80) >> 5));
    A = (uint8_t)r;
}
static ALWAYS void sub8(z80lite_t *z, uint8_t v) {
    const unsigned r = (unsigned)A - v;
    F = (uint8_t)(SZ[r & 0xFF] | (r >> 8 & CF) | NF | ((A ^ r ^ v) & HF) | (((v ^ A) & (A ^ r) & 0x80) >> 5));
    A = (uint8_t)r;
}
static ALWAYS void sbc8(z80lite_t *z, uint8_t v) {
    const unsigned r = (unsigned)A - v - (F & CF);
    F = (uint8_t)(SZ[r & 0xFF] | (r >> 8 & CF) | NF | ((A ^ r ^ v) & HF) | (((v ^ A) & (A ^ r) & 0x80) >> 5));
    A = (uint8_t)r;
}
static ALWAYS void cp8(z80lite_t *z, uint8_t v) {
    const unsigned r = (unsigned)A - v;
    F = (uint8_t)((SZ[r & 0xFF] & ~(YF | XF)) | (v & (YF | XF)) | (r >> 8 & CF) | NF | ((A ^ r ^ v) & HF) |
                  (((v ^ A) & (A ^ r) & 0x80) >> 5));
}
static ALWAYS void and8(z80lite_t *z, uint8_t v) { A &= v; F = SZP[A] | HF; }
static ALWAYS void xor8(z80lite_t *z, uint8_t v) { A ^= v; F = SZP[A]; }
static ALWAYS void or8(z80lite_t *z, uint8_t v) { A |= v; F = SZP[A]; }
static ALWAYS uint8_t inc8(z80lite_t *z, uint8_t v) { v++; F = (F & CF) | SZHV_INC[v]; return v; }
static ALWAYS uint8_t dec8(z80lite_t *z, uint8_t v) { v--; F = (F & CF) | SZHV_DEC[v]; return v; }

static ALWAYS void alu(z80lite_t *z, int op, uint8_t v) {
    switch (op) {
    case 0: add8(z, v); break;
    case 1: adc8(z, v); break;
    case 2: sub8(z, v); break;
    case 3: sbc8(z, v); break;
    case 4: and8(z, v); break;
    case 5: xor8(z, v); break;
    case 6: or8(z, v); break;
    default: cp8(z, v); break;
    }
}

static ALWAYS uint16_t add16(z80lite_t *z, uint16_t a, uint16_t b) {
    const uint32_t r = (uint32_t)a + b;
    WZ = (uint16_t)(a + 1);
    F = (uint8_t)((F & (SF | ZF | VF)) | (((a ^ r ^ b) >> 8) & HF) | ((r >> 16) & CF) | ((r >> 8) & (YF | XF)));
    return (uint16_t)r;
}
static ALWAYS void adc16(z80lite_t *z, uint16_t b) {
    const uint32_t r = (uint32_t)HL + b + (F & CF);
    WZ = (uint16_t)(HL + 1);
    F = (uint8_t)((((HL ^ r ^ b) >> 8) & HF) | ((r >> 16) & CF) | ((r >> 8) & (SF | YF | XF)) | ((r & 0xFFFF) ? 0 : ZF) |
                  (((b ^ HL ^ 0x8000) & (b ^ r) & 0x8000) >> 13));
    HL = (uint16_t)r;
}
static ALWAYS void sbc16(z80lite_t *z, uint16_t b) {
    const uint32_t r = (uint32_t)HL - b - (F & CF);
    WZ = (uint16_t)(HL + 1);
    F = (uint8_t)((((HL ^ r ^ b) >> 8) & HF) | NF | ((r >> 16) & CF) | ((r >> 8) & (SF | YF | XF)) | ((r & 0xFFFF) ? 0 : ZF) |
                  (((b ^ HL) & (HL ^ r) & 0x8000) >> 13));
    HL = (uint16_t)r;
}

// CB's shifts and rotates (op 0-7: RLC RRC RL RR SLA SRA SLL SRL)
static ALWAYS uint8_t rot(z80lite_t *z, int op, uint8_t v) {
    uint8_t r, c;
    switch (op) {
    case 0: c = v >> 7; r = (uint8_t)(v << 1 | c); break;
    case 1: c = v & 1; r = (uint8_t)(v >> 1 | c << 7); break;
    case 2: c = v >> 7; r = (uint8_t)(v << 1 | (F & CF)); break;
    case 3: c = v & 1; r = (uint8_t)(v >> 1 | (F & CF) << 7); break;
    case 4: c = v >> 7; r = (uint8_t)(v << 1); break;
    case 5: c = v & 1; r = (uint8_t)(v >> 1 | (v & 0x80)); break;
    case 6: c = v >> 7; r = (uint8_t)(v << 1 | 1); break;
    default: c = v & 1; r = v >> 1; break;
    }
    F = SZP[r] | c;
    return r;
}

static ALWAYS void bit(z80lite_t *z, int b, uint8_t v, uint8_t xy) {
    F = (uint8_t)((F & CF) | HF | (xy & (YF | XF)));
    if (!(v & (1 << b))) F |= ZF | PF;
    else if (b == 7) F |= SF;
}

static ALWAYS bool cond(const z80lite_t *z, int c) {
    switch (c) {
    case 0: return !(F & ZF);
    case 1: return F & ZF;
    case 2: return !(F & CF);
    case 3: return F & CF;
    case 4: return !(F & PF);
    case 5: return F & PF;
    case 6: return !(F & SF);
    default: return F & SF;
    }
}

// ---- the times --------------------------------------------------------------------------------
// the main table's cycles (a jump or call not taken; taken adds the rest)
static const uint8_t CC[256] = {
    4, 10, 7, 6, 4, 4, 7, 4, 4, 11, 7, 6, 4, 4, 7, 4,
    8, 10, 7, 6, 4, 4, 7, 4, 12, 11, 7, 6, 4, 4, 7, 4,
    7, 10, 16, 6, 4, 4, 7, 4, 7, 11, 16, 6, 4, 4, 7, 4,
    7, 10, 13, 6, 11, 11, 10, 4, 7, 11, 13, 6, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    5, 10, 10, 10, 10, 11, 7, 11, 5, 10, 10, 0, 10, 17, 7, 11,
    5, 10, 10, 11, 10, 11, 7, 11, 5, 4, 10, 11, 10, 0, 7, 11,
    5, 10, 10, 19, 10, 11, 7, 11, 5, 4, 10, 4, 10, 0, 7, 11,
    5, 10, 10, 4, 10, 11, 7, 11, 5, 6, 10, 4, 10, 0, 7, 11,
};
// ED's (anything else there is an 8-cycle NOP)
static ALWAYS int cc_ed(int op) {
    if (op >= 0x40 && op < 0x80) {
        switch (op & 7) {
        case 0: case 1: return 12;                          // IN r,(C) / OUT (C),r
        case 2: return 15;                                  // SBC/ADC HL,rr
        case 3: return 20;                                  // LD (nn),rr / LD rr,(nn)
        case 4: return 8;                                   // NEG
        case 5: return 14;                                  // RETN/RETI
        case 6: return 8;                                   // IM
        default: return op >= 0x70 ? 8 : op == 0x67 || op == 0x6F ? 18 : 9;  // ED 77 and 7F NOPs, RRD RLD, LD I,A etc.
        }
    }
    if (op >= 0xA0 && op < 0xC0 && (op & 7) < 4) return 16; // the block ones, once
    return 8;
}

// the main table's instructions that set the flags (the rest leave Q_NONE
// in z->q, which SCF and CCF read)
static ALWAYS bool sets_flags(int op) {
    if (op >= 0x80 && op < 0xC0) return true;               // ALU A,r
    if (op >= 0xC0) return (op & 7) == 6;                   // ALU A,n
    if (op >= 0x40) return false;                           // LD r,r'
    switch (op & 7) {
    case 1: return op & 8;                                  // ADD HL,rr
    case 4: case 5: case 7: return true;                    // INC r, DEC r, RLCA .. CCF
    default: return false;
    }
}

// ---- an instruction ---------------------------------------------------------------------------
// The prefix: xy is HL, IX or IY; px says which (0 none). They're the
// parameters of exec_body() and cb(), and the macros below pass them on (in
// the copy of exec_body() the loop has for no prefix, they're constants).
static ALWAYS uint8_t get_r_(z80lite_t *z, z80lite_pair_t *xy, int r) {    // r != 6, IXH/IXL under a prefix
    switch (r) {
    case 0: return B;
    case 1: return C;
    case 2: return D;
    case 3: return E;
    case 4: return xy->b.h;
    case 5: return xy->b.l;
    default: return A;
    }
}
static ALWAYS void set_r_(z80lite_t *z, z80lite_pair_t *xy, int r, uint8_t v) {
    switch (r) {
    case 0: B = v; break;
    case 1: C = v; break;
    case 2: D = v; break;
    case 3: E = v; break;
    case 4: xy->b.h = v; break;
    case 5: xy->b.l = v; break;
    default: A = v; break;
    }
}
// H and L themselves (beside (IX+d))
static ALWAYS uint8_t get_real(z80lite_t *z, int r) { return get_r_(z, &z->hl, r); }
static ALWAYS void set_real(z80lite_t *z, int r, uint8_t v) { set_r_(z, &z->hl, r, v); }
// (HL), or (IX+d) with d fetched
static ALWAYS uint16_t ea_(z80lite_t *z, int px, z80lite_pair_t *xy) {
    if (!px) return HL;
    const uint16_t a = (uint16_t)(xy->w + (int8_t)fetch(z));
    WZ = a;
    return a;
}
static ALWAYS uint16_t *rp_(z80lite_t *z, z80lite_pair_t *xy, int p) {     // BC DE HL SP (HL: xy)
    switch (p) {
    case 0: return &BC;
    case 1: return &DE;
    case 2: return &xy->w;
    default: return &SP;
    }
}
static ALWAYS uint16_t *rp2_(z80lite_t *z, z80lite_pair_t *xy, int p) {    // BC DE HL AF
    switch (p) {
    case 0: return &BC;
    case 1: return &DE;
    case 2: return &xy->w;
    default: return &z->af.w;
    }
}

// where B C D E H L A are in a z80lite_t, by their number in an opcode
// (6, (HL), isn't one)
static const uint8_t REG[8] = {
    offsetof(z80lite_t, bc.b.h), offsetof(z80lite_t, bc.b.l), offsetof(z80lite_t, de.b.h), offsetof(z80lite_t, de.b.l),
    offsetof(z80lite_t, hl.b.h), offsetof(z80lite_t, hl.b.l), 0, offsetof(z80lite_t, af.b.h),
};

#define get_r(r) get_r_(z, xy, r)
#define set_r(r, v) set_r_(z, xy, r, v)
#define ea() ea_(z, px, xy)
#define rp(p) rp_(z, xy, p)
#define rp2(p) rp2_(z, xy, p)

static HOT int cb(z80lite_t *z, int px, z80lite_pair_t *xy);
static HOT int cb_hl(z80lite_t *z);
static HOT int ed(z80lite_t *z);
static HOT int prefixed(z80lite_t *z, uint8_t op);

// one instruction from the main table, its prefix (if any) and opcode
// fetched
static ALWAYS int exec_body(z80lite_t *z, uint8_t op, const int px, z80lite_pair_t *const xy) {
    int cyc = CC[op];
    const int y = op >> 3 & 7, zz = op & 7, p = y >> 1;
    const uint8_t q = z->q;                 // (the last instruction's, for SCF and CCF)
    if (op != 0xCB && op != 0xED && op != 0xDD && op != 0xFD) z->q = sets_flags(op) ? 0 : Q_NONE;
    switch (op >> 6) {
    case 1:                                 // LD r,r' and HALT
        if (op == 0x76) {
            z->halt = 1;                    // (PC past it, as MAME's)
            z->attn = 1;
        } else if (zz == 6) {
            set_real(z, y, rd(z, ea()));
            if (px) cyc += 8;
        } else if (y == 6) {
            wr(z, ea(), get_real(z, zz));
            if (px) cyc += 8;
        } else {
            set_r(y, get_r(zz));
        }
        return cyc + (px ? 4 : 0);
    case 2:                                 // ALU A,r
        if (zz == 6) {
            alu(z, y, rd(z, ea()));
            if (px) cyc += 8;
        } else {
            alu(z, y, get_r(zz));
        }
        return cyc + (px ? 4 : 0);
    }
    switch (op) {
    case 0x00: break;                                               // NOP
    case 0x01: case 0x11: case 0x21: case 0x31: *rp(p) = fetch16(z); break;   // LD rr,nn
    case 0x02: wr(z, BC, A); z->wz.b.l = (uint8_t)(BC + 1); z->wz.b.h = A; break;
    case 0x12: wr(z, DE, A); z->wz.b.l = (uint8_t)(DE + 1); z->wz.b.h = A; break;
    case 0x0A: A = rd(z, BC); WZ = (uint16_t)(BC + 1); break;
    case 0x1A: A = rd(z, DE); WZ = (uint16_t)(DE + 1); break;
    case 0x22: { const uint16_t a = fetch16(z); wr16(z, a, xy->w); WZ = (uint16_t)(a + 1); break; }
    case 0x2A: { const uint16_t a = fetch16(z); xy->w = rd16(z, a); WZ = (uint16_t)(a + 1); break; }
    case 0x32: { const uint16_t a = fetch16(z); wr(z, a, A); z->wz.b.l = (uint8_t)(a + 1); z->wz.b.h = A; break; }
    case 0x3A: { const uint16_t a = fetch16(z); A = rd(z, a); WZ = (uint16_t)(a + 1); break; }
    case 0x03: case 0x13: case 0x23: case 0x33: (*rp(p))++; break;  // INC rr
    case 0x0B: case 0x1B: case 0x2B: case 0x3B: (*rp(p))--; break;  // DEC rr
    case 0x09: case 0x19: case 0x29: case 0x39: xy->w = add16(z, xy->w, *rp(p)); break;   // ADD HL,rr
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C: set_r(y, inc8(z, get_r(y))); break;
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D: set_r(y, dec8(z, get_r(y))); break;
    case 0x34: { const uint16_t a = ea(); wr(z, a, inc8(z, rd(z, a))); if (px) cyc += 8; break; }
    case 0x35: { const uint16_t a = ea(); wr(z, a, dec8(z, rd(z, a))); if (px) cyc += 8; break; }
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x3E: set_r(y, fetch(z)); break;
    case 0x36: { const uint16_t a = ea(); wr(z, a, fetch(z)); if (px) cyc += 5; break; }
    case 0x07: A = (uint8_t)(A << 1 | A >> 7); F = (F & (SF | ZF | PF)) | (A & (YF | XF | CF)); break;   // RLCA
    case 0x0F: F = (F & (SF | ZF | PF)) | (A & CF); A = (uint8_t)(A >> 1 | A << 7); F |= A & (YF | XF); break;   // RRCA
    case 0x17: { const uint8_t r = (uint8_t)(A << 1 | (F & CF)); F = (F & (SF | ZF | PF)) | (A >> 7) | (r & (YF | XF)); A = r; break; }   // RLA
    case 0x1F: { const uint8_t r = (uint8_t)(A >> 1 | (F & CF) << 7); F = (F & (SF | ZF | PF)) | (A & CF) | (r & (YF | XF)); A = r; break; }   // RRA
    case 0x08: { const uint16_t t = z->af.w; z->af.w = z->af2.w; z->af2.w = t; break; }   // EX AF,AF'
    case 0x10: { const int8_t d = (int8_t)fetch(z); if (--B) { PC = (uint16_t)(PC + d); WZ = PC; cyc += 5; } break; }   // DJNZ
    case 0x18: { const int8_t d = (int8_t)fetch(z); const uint16_t from = PC; PC = (uint16_t)(PC + d); WZ = PC; BACK(from); break; }   // JR
    case 0x20: case 0x28: case 0x30: case 0x38: {                   // JR cc
        const int8_t d = (int8_t)fetch(z);
        if (cond(z, y - 4)) { const uint16_t from = PC; PC = (uint16_t)(PC + d); WZ = PC; cyc += 5; BACK(from); }
        break;
    }
    case 0x27: {                                                    // DAA
        uint8_t a = A;
        if (F & NF) {
            if ((F & HF) || (A & 15) > 9) a -= 6;
            if ((F & CF) || A > 0x99) a -= 0x60;
        } else {
            if ((F & HF) || (A & 15) > 9) a += 6;
            if ((F & CF) || A > 0x99) a += 0x60;
        }
        F = (uint8_t)((F & (CF | NF)) | (A > 0x99) | ((A ^ a) & HF) | SZP[a]);
        A = a;
        break;
    }
    case 0x2F: A ^= 0xFF; F = (F & (SF | ZF | PF | CF)) | HF | NF | (A & (YF | XF)); break;   // CPL
    // SCF and CCF: X and Y from A, and from F too when the last instruction
    // left the flags alone (the Q rule; MAME's z80.cpp does the same)
    case 0x37: F = (uint8_t)((F & (SF | ZF | PF)) | CF | (((F & q) | A) & (YF | XF))); break;   // SCF
    case 0x3F: F = (uint8_t)(((F & (SF | ZF | PF | CF)) | ((F & CF) << 4) | (((F & q) | A) & (YF | XF))) ^ CF); break;   // CCF
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:   // RET cc
        if (cond(z, y)) { PC = pop(z); WZ = PC; cyc += 6; }
        break;
    case 0xC1: case 0xD1: case 0xE1: case 0xF1: *rp2(p) = pop(z); break;
    case 0xC5: case 0xD5: case 0xE5: case 0xF5: push(z, *rp2(p)); break;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA: {   // JP cc
        const uint16_t a = fetch16(z);
        WZ = a;
        if (cond(z, y)) { const uint16_t from = PC; PC = a; BACK(from); }
        break;
    }
    case 0xC3: { const uint16_t a = fetch16(z), from = PC; PC = a; WZ = PC; BACK(from); break; }
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC: {   // CALL cc
        const uint16_t a = fetch16(z);
        WZ = a;
        if (cond(z, y)) { push(z, PC); PC = a; cyc += 7; }
        break;
    }
    case 0xCD: { const uint16_t a = fetch16(z); push(z, PC); PC = a; WZ = a; break; }
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE: alu(z, y, fetch(z)); break;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:   // RST
        push(z, PC);
        PC = (uint16_t)(op & 0x38);
        WZ = PC;
        break;
    case 0xC9: PC = pop(z); WZ = PC; break;
    case 0xCB: return px ? cb(z, px, xy) + 4 : cb_hl(z);
    case 0xD3: {                                                    // OUT (n),A
        const uint8_t n = fetch(z);
        out_port(z, (uint16_t)(A << 8 | n), A);
        z->wz.b.l = (uint8_t)(n + 1);
        z->wz.b.h = A;
        break;
    }
    case 0xDB: {                                                    // IN A,(n)
        const uint16_t port = (uint16_t)(A << 8 | fetch(z));
        A = in_port(z, port);
        WZ = (uint16_t)(port + 1);
        break;
    }
    case 0xD9: {                                                    // EXX
        uint16_t t;
        t = BC; BC = z->bc2.w; z->bc2.w = t;
        t = DE; DE = z->de2.w; z->de2.w = t;
        t = HL; HL = z->hl2.w; z->hl2.w = t;
        break;
    }
    case 0xE3: {                                                    // EX (SP),HL
        const uint16_t t = rd16(z, SP);
        wr(z, (uint16_t)(SP + 1), xy->b.h);     // (high byte first)
        wr(z, SP, xy->b.l);
        xy->w = t;
        WZ = t;
        break;
    }
    case 0xE9: PC = xy->w; break;                                   // JP (HL)
    case 0xEB: { const uint16_t t = DE; DE = HL; HL = t; break; }   // EX DE,HL (never IX)
    case 0xED: return ed(z);
    case 0xF3: z->iff1 = z->iff2 = 0; break;                        // DI
    case 0xFB: z->iff1 = z->iff2 = 1; z->ei = 1; z->attn = 1; break;    // EI
    case 0xF9: SP = xy->w; break;                                   // LD SP,HL
    case 0xDD: case 0xFD:                                           // a prefix (a second one: prefixed() takes it)
        if (!px) return prefixed(z, op);
        break;
    default: break;
    }
    return cyc + (px ? 4 : 0);
}

// CB: under a prefix, DD CB d op: the operand is (IX+d), and a register
// in the op gets the result too (undocumented)
static ALWAYS int cb_body(z80lite_t *z, const int px, z80lite_pair_t *const xy) {
    uint16_t a = 0;
    if (px) a = ea();
    const uint8_t op = fetch(z);
    const int y = op >> 3 & 7, zz = op & 7;
    if (!px) z->r++;
    z->q = op >= 0x80 ? Q_NONE : 0;         // (RES and SET leave the flags alone)
    const bool mem = px || zz == 6;
    if (mem && !px) a = HL;
    uint8_t *const reg = (uint8_t *)z + REG[zz];
    const uint8_t v = mem ? rd(z, a) : *reg;
    uint8_t r;
    switch (op >> 6) {
    case 0: r = rot(z, y, v); break;
    case 1:
        bit(z, y, v, px ? (uint8_t)(a >> 8) : mem ? z->wz.b.h : v);
        return px ? 16 : mem ? 12 : 8;
    case 2: r = (uint8_t)(v & ~(1 << y)); break;
    default: r = (uint8_t)(v | 1 << y); break;
    }
    if (mem) {
        wr(z, a, r);
        if (px && zz != 6) *reg = r;
        return px ? 19 : 15;
    }
    *reg = r;
    return 8;
}

// (two copies: with no prefix, fewer registers to save)
static NOINLINE HOT int cb(z80lite_t *z, const int px, z80lite_pair_t *const xy) { return cb_body(z, px, xy); }
static NOINLINE HOT int cb_hl(z80lite_t *z) { return cb_body(z, 0, &z->hl); }

// INI .. OTDR's flags (and, repeating, the ones an interrupted one leaves)
static ALWAYS void block_io(z80lite_t *z, uint8_t v, unsigned t, bool rep, int *cyc) {
    F = (uint8_t)(SZ[B] | (v & 0x80 ? NF : 0) | (t > 0xFF ? HF | CF : 0) | (SZP[(t & 7) ^ B] & PF));
    if (rep && B) {
        PC -= 2;
        *cyc += 5;
        F = (F & ~(YF | XF)) | ((PC >> 8) & (YF | XF));
        uint8_t pv;
        if (F & CF) {
            F &= ~HF;
            if (v & 0x80) {
                pv = (uint8_t)((B - 1) & 7);
                if ((B & 15) == 0) F |= HF;
            } else {
                pv = (uint8_t)((B + 1) & 7);
                if ((B & 15) == 15) F |= HF;
            }
        } else {
            pv = B & 7;
        }
        F ^= (SZP[pv] & PF) ^ PF;           // (MAME's: P is set when it and pv's parity agree)
    }
}

// the ED instructions that set the flags
static ALWAYS bool ed_sets_flags(int op) {
    if (op >= 0x40 && op < 0x80) {
        switch (op & 7) {
        case 0: case 2: case 4: return true;                // IN r,(C), SBC/ADC HL,rr, NEG
        case 7: return op == 0x57 || op == 0x5F || op == 0x67 || op == 0x6F;   // LD A,I  LD A,R  RRD  RLD
        default: return false;
        }
    }
    return op >= 0xA0 && op < 0xC0 && (op & 7) < 4;        // the block ones
}

static HOT int ed(z80lite_t *z) {
    z80lite_pair_t *const xy = &z->hl;      // (DD ED: ED's alone)
    const uint8_t op = fetch(z);
    z->r++;
    z->q = ed_sets_flags(op) ? 0 : Q_NONE;
    const int y = op >> 3 & 7, p = y >> 1;
    int cyc = cc_ed(op);
    if (op >= 0x40 && op < 0x80) {
        switch (op & 7) {
        case 0: {                                                   // IN r,(C) (ED 70: the flags only)
            const uint8_t v = in_port(z, BC);
            if (y != 6) set_real(z, y, v);
            WZ = (uint16_t)(BC + 1);
            F = (F & CF) | SZP[v];
            break;
        }
        case 1:                                                     // OUT (C),r (ED 71: 0)
            out_port(z, BC, y == 6 ? 0 : get_real(z, y));
            WZ = (uint16_t)(BC + 1);
            break;
        case 2: {
            const uint16_t v = *rp(p);
            if (op & 8) adc16(z, v);
            else sbc16(z, v);
            break;
        }
        case 3: {
            const uint16_t a = fetch16(z);
            uint16_t *const r = rp(p);
            if (op & 8) *r = rd16(z, a);
            else wr16(z, a, *r);
            WZ = (uint16_t)(a + 1);
            break;
        }
        case 4: { const uint8_t v = A; A = 0; sub8(z, v); break; } // NEG
        case 5: z->iff1 = z->iff2; PC = pop(z); WZ = PC; z->attn = 1; break;   // RETN, RETI
        case 6: z->im = (y & 3) < 2 ? 0 : (uint8_t)((y & 3) - 1); break;    // IM 0, 0, 1, 2
        default:
            switch (op) {
            case 0x47: z->i = A; break;
            case 0x4F: z->r = A; z->r7 = A & 0x80; break;
            case 0x57: A = z->i; F = (F & CF) | SZ[A] | (z->iff2 ? PF : 0); break;
            case 0x5F: A = z80lite_r(z); F = (F & CF) | SZ[A] | (z->iff2 ? PF : 0); break;
            case 0x67: {                                            // RRD
                const uint8_t m = rd(z, HL);
                WZ = (uint16_t)(HL + 1);
                wr(z, HL, (uint8_t)(A << 4 | m >> 4));
                A = (uint8_t)((A & 0xF0) | (m & 15));
                F = (F & CF) | SZP[A];
                break;
            }
            case 0x6F: {                                            // RLD
                const uint8_t m = rd(z, HL);
                WZ = (uint16_t)(HL + 1);
                wr(z, HL, (uint8_t)(m << 4 | (A & 15)));
                A = (uint8_t)((A & 0xF0) | m >> 4);
                F = (F & CF) | SZP[A];
                break;
            }
            default: break;                                         // ED 77, 7F: NOP
            }
        }
        return cyc;
    }
    if (op >= 0xA0 && op < 0xC0 && (op & 7) < 4) {
        const bool dec = op & 8, rep = op & 0x10;
        switch (op & 3) {
        case 0: {                                                   // LDI LDD LDIR LDDR
            const uint8_t v = rd(z, HL);
            wr(z, DE, v);
            HL = dec ? HL - 1 : HL + 1;
            DE = dec ? DE - 1 : DE + 1;
            BC--;
            const uint8_t n = (uint8_t)(v + A);
            F = (uint8_t)((F & (SF | ZF | CF)) | (n & XF) | ((n << 4) & YF) | (BC ? PF : 0));
            if (rep && BC) { PC -= 2; WZ = (uint16_t)(PC + 1); cyc += 5; F = (F & ~(YF | XF)) | ((PC >> 8) & (YF | XF)); }
            break;
        }
        case 1: {                                                   // CPI CPD CPIR CPDR
            const uint8_t v = rd(z, HL);
            uint8_t r = (uint8_t)(A - v);
            WZ = dec ? WZ - 1 : WZ + 1;
            HL = dec ? HL - 1 : HL + 1;
            BC--;
            F = (uint8_t)((F & CF) | (SZ[r] & ~(YF | XF)) | ((A ^ v ^ r) & HF) | NF);
            if (F & HF) r--;
            F |= (r & XF) | ((r << 4) & YF);
            if (BC) F |= PF;
            if (rep && BC && !(F & ZF)) { PC -= 2; WZ = (uint16_t)(PC + 1); cyc += 5; F = (F & ~(YF | XF)) | ((PC >> 8) & (YF | XF)); }
            break;
        }
        case 2: {                                                   // INI IND INIR INDR
            const uint8_t v = in_port(z, BC);
            WZ = dec ? BC - 1 : BC + 1;
            B--;
            wr(z, HL, v);
            HL = dec ? HL - 1 : HL + 1;
            block_io(z, v, (unsigned)((dec ? C - 1 : C + 1) & 0xFF) + v, rep, &cyc);
            break;
        }
        default: {                                                  // OUTI OUTD OTIR OTDR
            const uint8_t v = rd(z, HL);
            B--;
            WZ = dec ? BC - 1 : BC + 1;
            out_port(z, BC, v);
            HL = dec ? HL - 1 : HL + 1;
            block_io(z, v, (unsigned)L + v, rep, &cyc);
            break;
        }
        }
        return cyc;
    }
    return cyc;                                                     // the rest: NOP
}

// exec(): a case for each opcode, exec_body() made with the opcode a
// constant, so that the registers and conditions in it are worked out
// as it's built, not each time it runs.
#define X(o) case o: return exec_body(z, o, 0, &z->hl);
#define X8(o) X(o) X(o + 1) X(o + 2) X(o + 3) X(o + 4) X(o + 5) X(o + 6) X(o + 7)
#define X64(o) X8(o) X8(o + 8) X8(o + 16) X8(o + 24) X8(o + 32) X8(o + 40) X8(o + 48) X8(o + 56)
static ALWAYS int exec(z80lite_t *z, uint8_t op) {
    switch (op) {
    X64(0x00) X64(0x40) X64(0x80) X64(0xC0)
    }
    return 4;
}
#undef X
#undef X8
#undef X64

// ---- interrupts and the loop ------------------------------------------------------------------
// (a prefixed one: exec_body() for them all)
static NOINLINE HOT int exec_any(z80lite_t *z, uint8_t op, int px, z80lite_pair_t *xy) { return exec_body(z, op, px, xy); }

// a DD or FD prefix (and any after it: the last one counts), then the
// instruction
static NOINLINE HOT int prefixed(z80lite_t *z, uint8_t op) {
    int px, extra = 0;
    z80lite_pair_t *xy;
    do {
        px = op == 0xDD ? 1 : 2;
        xy = op == 0xDD ? &z->ix : &z->iy;
        op = fetch(z);
        z->r++;
        if (op == 0xDD || op == 0xFD) extra += 4;
    } while (op == 0xDD || op == 0xFD);
    z->q = Q_NONE;                          // (the prefix left the flags alone, as MAME counts it)
    if (op == 0xED) return ed(z) + extra + 4;   // DD ED: the ED one alone
    return exec_any(z, op, px, xy) + extra;
}

// an NMI or interrupt to take now: its cycles, or 0
static NOINLINE HOT int take_int(z80lite_t *z) {
    if (z->nmi) {
        z->nmi = 0;
        z->ei = 0;
        z->halt = 0;
        z->iff1 = 0;
        z->r++;
        push(z, PC);
        PC = 0x66;
        WZ = PC;
        return 11;
    }
    if (z->irq && z->iff1 && !z->ei) {
        z->halt = 0;
        z->iff1 = z->iff2 = 0;
        z->r++;
        const uint8_t v = z->ack ? z->ack(z) : z->irq_data;
        int cyc = 2;
        if (z->im == 2) {                   // a CALL to the vector at I * 256 + the byte
            push(z, PC);
            PC = rd16(z, (uint16_t)(z->i << 8 | v));
            cyc = 19;
        } else if (z->im == 1 || (v & 0xC7) == 0xC7) {      // RST 38h, or IM 0's RST
            push(z, PC);
            PC = z->im == 1 ? 0x38 : v & 0x38;
            cyc = 13;
        } else if (v == 0xFB) {             // IM 0's EI
            z->iff1 = z->iff2 = 1;
            z->ei = 1;
            z->attn = 1;
            cyc = 6;
        }                                   // (IM 0's anything else: nothing, as MAME's)
        WZ = PC;
        return cyc;
    }
    return 0;
}

HOT int z80lite_run(z80lite_t *z, int cycles) {
    int done = 0;
    z->idle_pc = 0x10000;                   // (the world may have changed since the last slice)
    z->idle = 0;
    z->stop = 0;
    z->attn = 1;                            // (and the interrupt lines)
    while (done < cycles) {
        if (__builtin_expect(z->attn, 0)) {
            z->attn = 0;
            if (z->stop) break;             // z80lite_abort
            if (z->idle) {                  // waiting: the rest of the slice passes
                const int rest = cycles - done;
                z->r = (uint8_t)(z->r + rest / 4);
                z->idle_cycles += (uint32_t)rest;
                done = cycles;
                break;
            }
            const int c = take_int(z);
            if (c) {
                done += c;
                continue;
            }
            if (z->ei) {                    // the instruction after EI runs first
                z->ei = 0;
                z->attn = 1;
                if (z->halt) {
                    z->r++;
                    done += 4;
                    continue;
                }
            } else if (z->halt) {           // halted, nothing to wake it: its NOPs at once
                const int n = (cycles - done + 3) >> 2;
                z->r = (uint8_t)(z->r + n);
                done += n * 4;
                break;
            }
        }
#ifdef Z80LITE_TRACE
        Z80LITE_TRACE(z, done);             // (a test's: before each instruction, with the cycles so far)
        if (z->stop) break;                 // (and an abort from it stops before the instruction)
#endif
        const uint8_t op = fetch(z);
        z->r++;
        done += exec(z, op);
    }
    z->total += (uint64_t)done;
    return done;
}

int z80lite_step(z80lite_t *z) { return z80lite_run(z, 1); }

void z80lite_set_irq(z80lite_t *z, int state) {
    z->irq = state != 0;
    z->idle = 0;
    z->attn = 1;
}

void z80lite_set_nmi(z80lite_t *z, int state) {
    if (state && !z->nmi_line) {
        z->nmi = 1;
        z->idle = 0;
        z->attn = 1;
    }
    z->nmi_line = state != 0;
}

void z80lite_abort(z80lite_t *z) {
    z->stop = 1;
    z->attn = 1;
}

void z80lite_reset(z80lite_t *z) {
    PC = 0;
    WZ = 0;
    z->i = z->r = z->r7 = z->im = 0;
    z->iff1 = z->iff2 = z->halt = z->ei = 0;
    z->nmi = 0;
    z->total = 0;
    z->attn = 1;
}

void z80lite_init(z80lite_t *z) {
    for (int i = 0; i < 256; i++) {
        const uint8_t s = (uint8_t)((i & SF) | (i & (YF | XF)) | (i ? 0 : ZF));
        int par = i;
        par ^= par >> 4;
        par ^= par >> 2;
        par ^= par >> 1;
        SZ[i] = s;
        SZP[i] = (uint8_t)(s | ((par & 1) ? 0 : PF));
        SZHV_INC[i] = (uint8_t)(s | (i == 0x80 ? VF : 0) | ((i & 15) == 0 ? HF : 0));
        SZHV_DEC[i] = (uint8_t)(s | NF | (i == 0x7F ? VF : 0) | ((i & 15) == 15 ? HF : 0));
    }
    memset(z, 0, sizeof *z);
    z->af.w = 0xFFFF;                       // (as the Z80 powers on)
    SP = 0xFFFF;
    z->ix.w = z->iy.w = 0xFFFF;             // (as MAME's)
    z->irq_data = 0xFF;
    z80lite_reset(z);
}

// (the macros end here, so a test can include this file next to another core)
#undef HOT
#undef ALWAYS
#undef NOINLINE
#undef A
#undef F
#undef B
#undef C
#undef D
#undef E
#undef H
#undef L
#undef BC
#undef DE
#undef HL
#undef SP
#undef PC
#undef WZ
#undef Q_NONE
#undef BACK
#undef get_r
#undef set_r
#undef ea
#undef rp
#undef rp2
