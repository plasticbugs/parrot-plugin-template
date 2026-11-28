// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Parrot Cart contributors
//
// test.c -- lib/z80lite's test, run by lib/test.sh. Two parts, no ROM:
//
// The fuzz runs random code on z80lite beside lib/z80 (MAME 0.288's Z80),
// instruction by instruction. lib/z80 runs; before each of its instructions
// (Z80_TRACE) z80lite runs the one before, and any interrupt lib/z80 took
// after it, and the two are compared: every register, the flags with X and
// Y, WZ, R, IM and the IFFs, HALT, the cycle count, every write to a hook and
// every OUT, and the memory now and then. IN gives the same made-up values to
// both. IRQs (with a random byte on the bus, in whatever IM the code set) and
// NMIs come now and then. It runs three ways: all memory through hooks; a ROM
// window, a hooked area and a RAM window; code in RAM with the ROM window
// pointed at it.
//
// The cases check the rest: ports, IM 0, 1 and 2, the ack hook and the
// level of the line, NMI, HALT, the EI delay, block I/O, the windows,
// z80lite_abort, two CPUs, and the idle skip.
//
//   cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -I lib lib/z80lite/test.c -o /tmp/z80lite-test
//   /tmp/z80lite-test
//   /tmp/z80lite-test 30000000 7     # 30 million instructions each way (1 million by default), seed 7
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80/z80.h"
#include "z80lite/z80lite.h"         // (z80lite.c is included below, with its trace)

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- the fuzz: lib/z80 (B, as MAME runs it) beside z80lite (A) --------------------------------
static uint8_t memA[0x10000], memB[0x10000];
typedef struct { uint16_t a; uint8_t v, out; } event_t;    // a write to a hook, or an OUT
static event_t logA[64], logB[64];
static int nA, nB;
static uint32_t insA, insB;             // the INs so far: the next one's value
static int mode;                        // 0 all hooks, 1 ROM / hooks / RAM, 2 all RAM with the ROM window on it
static uint8_t vecA, vecB;              // the byte on the bus at the next acknowledge
static bool hold;                       // the acknowledge leaves the line up (it comes down later)

static uint8_t in_value(uint16_t port, uint32_t n) {
    uint32_t h = (port * 0x9E3779B1u) ^ (n * 0x85EBCA77u);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    return (uint8_t)(h >> 24);
}
static void note(event_t *log, int *n, uint16_t a, uint8_t v, uint8_t out) {
    if (*n < 64) log[*n] = (event_t){a, v, out};
    (*n)++;
}
// mode 1: 0x0000-0x3FFF ROM (a write is dropped), 0x4000-0x7FFF the hooks', 0x8000- RAM
static void b_wr(uint16_t a, uint8_t v) {
    note(logB, &nB, a, v, 0);
    if (mode != 1 || a >= 0x4000) memB[a] = v;
}
static int b_ack(void) {
    if (!hold) z80_set_irq(0, 0);       // (HOLD_LINE)
    return vecB;
}
static void b_trace(void);

#define Z80_CPUS 1
#define Z80_RD(cpu, a) memB[(a)]
#define Z80_WR(cpu, a, d) b_wr(a, d)
#define Z80_IN(cpu, port) in_value(port, insB++)
#define Z80_OUT(cpu, port, d) note(logB, &nB, port, d, 1)
#define Z80_IRQ_VECTOR(cpu) b_ack()
#define Z80_TRACE(cpu) b_trace()
#include "z80/z80.c"

// z80lite with its trace (a test's only)
static void a_trace(z80lite_t *z, int done);
#define Z80LITE_TRACE(z, done) a_trace(z, done)
#include "z80lite/z80lite.c"

static z80lite_t za;
static uint8_t a_rd(z80lite_t *z, uint16_t a) { (void)z; return memA[a]; }
static void a_wr(z80lite_t *z, uint16_t a, uint8_t v) {
    (void)z;
    note(logA, &nA, a, v, 0);
    if (mode != 1 || a >= 0x4000) memA[a] = v;
}
static uint8_t a_in(z80lite_t *z, uint16_t port) { (void)z; return in_value(port, insA++); }
static void a_out(z80lite_t *z, uint16_t port, uint8_t v) { (void)z; note(logA, &nA, port, v, 1); }
static uint8_t a_ack(z80lite_t *z) {
    if (!hold) z80lite_set_irq(z, 0);
    return vecA;
}

static uint64_t rng;
static uint32_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)rng;
}
// random code into both memories, a prefix (ED, CB, DD or FD) one byte in four
static void fill(uint16_t at, int n) {
    static const uint8_t prefix[4] = {0xED, 0xCB, 0xDD, 0xFD};
    for (int k = 0; k < n; k++) {
        const uint32_t r = rnd();
        const uint8_t v = r >> 30 == 0 ? prefix[r >> 28 & 3] : (uint8_t)r;
        memA[(uint16_t)(at + k)] = memB[(uint16_t)(at + k)] = v;
    }
}

static long limit, diffs, irqs, nmis, halts;
static long a_at, b_at;                 // the instructions each has come to
static int a_done;                      // z80lite's cycles so far in its run, at its trace
static bool fuzzing, stopped, woke;     // woke: both sat in a HALT, and an NMI woke them
static uint16_t last_pc;
static uint8_t last_op[4];

static bool takes_int(const z80lite_t *z) { return z->nmi || (z->irq && z->iff1 && !z->ei); }

static void dump(const char *why, const z80_regs_t *r) {
    printf("  mode %d instruction %ld: %s after %02X %02X %02X %02X at %04X\n", mode, a_at, why, last_op[0], last_op[1], last_op[2],
           last_op[3], last_pc);
    printf("  lib/z80  pc %04X sp %04X af %04X bc %04X de %04X hl %04X ix %04X iy %04X wz %04X r %02X i %02X im %d iff %d%d halt %d cycles %llu\n",
           r->pc, r->sp, r->af, r->bc, r->de, r->hl, r->ix, r->iy, r->wz, r->r, r->i, r->im, r->iff1, r->iff2, r->halt,
           (unsigned long long)z80_total(0));
    printf("  z80lite  pc %04X sp %04X af %04X bc %04X de %04X hl %04X ix %04X iy %04X wz %04X r %02X i %02X im %d iff %d%d halt %d cycles %llu\n",
           za.pc.w, za.sp.w, za.af.w, za.bc.w, za.de.w, za.hl.w, za.ix.w, za.iy.w, za.wz.w, z80lite_r(&za), za.i, za.im, za.iff1,
           za.iff2, za.halt, (unsigned long long)(za.total + (uint64_t)a_done));
}

static void copy_regs(const z80_regs_t *r) {    // lib/z80's registers into z80lite
    za.pc.w = r->pc; za.sp.w = r->sp; za.af.w = r->af; za.bc.w = r->bc; za.de.w = r->de; za.hl.w = r->hl;
    za.ix.w = r->ix; za.iy.w = r->iy; za.wz.w = r->wz; za.af2.w = r->af2; za.bc2.w = r->bc2; za.de2.w = r->de2;
    za.hl2.w = r->hl2; za.r = r->r & 0x7F; za.r7 = r->r & 0x80; za.i = r->i; za.im = r->im;
    za.iff1 = r->iff1; za.iff2 = r->iff2; za.halt = r->halt;
}

// z80lite, before each of its instructions: lib/z80 runs up to the same one
static void a_trace(z80lite_t *z, int done) {
    if (!fuzzing) return;               // (the cases' CPUs)
    if (stopped) {
        z80lite_abort(z);
        return;
    }
    a_at++;
    a_done = done;
    while (b_at < a_at && !stopped) {
        z80_run(0, 1 << 16);
        if (b_at < a_at && z80_halted(0)) {     // (nothing can wake it: the lines change only here)
            printf("  mode %d instruction %ld: lib/z80 sits in a HALT, z80lite doesn't\n", mode, a_at);
            diffs++;
            stopped = true;
        }
    }
    if (a_at > limit) stopped = true;   // (b_trace has compared the memory)
    if (stopped) z80lite_abort(z);
}

// lib/z80, before each of its instructions: the two compared, then the
// lines and the code changed for both, and back to z80lite
static void b_trace(void) {
    b_at++;
    z80_regs_t r;
    z80_regs(0, &r);
    if (b_at != a_at) {                 // lib/z80 ran on where z80lite sat in a HALT
        dump("lib/z80 didn't stay in the HALT", &r);
        diffs++;
        stopped = true;
        z80_abort(0);
        return;
    }
    if (woke) {                         // (the NOPs' cycles and R aren't compared)
        za.total = z80_total(0) - (uint64_t)a_done;
        za.r = r.r & 0x7F;
        woke = false;
    }
    int bad = 0;
    if (r.pc != za.pc.w || r.sp != za.sp.w || r.af != za.af.w || r.bc != za.bc.w || r.de != za.de.w || r.hl != za.hl.w ||
        r.ix != za.ix.w || r.iy != za.iy.w || r.af2 != za.af2.w || r.bc2 != za.bc2.w || r.de2 != za.de2.w ||
        r.hl2 != za.hl2.w || r.i != za.i || r.im != za.im || r.iff1 != za.iff1 || r.iff2 != za.iff2 || r.halt != za.halt)
        bad |= 1;
    if (r.wz != za.wz.w) bad |= 2;
    if (r.r != z80lite_r(&za)) bad |= 4;
    if (z80_total(0) != za.total + (uint64_t)a_done) bad |= 8;
    if (nA != nB || memcmp(logA, logB, sizeof(event_t) * (size_t)(nA < 64 ? nA : 64))) bad |= 16;
    if ((a_at % 1024 == 0 || a_at > limit) && memcmp(memA, memB, sizeof memA)) bad |= 32;
    if (bad) {
        static const char *const what[] = {"registers", "WZ", "R", "cycles", "writes", "memory"};
        char why[64] = "";
        for (int k = 0; k < 6; k++)
            if (bad & 1 << k) { strcat(why, what[k]); strcat(why, " "); }
        dump(why, &r);
        if (bad & 16) {
            printf("  lib/z80 :");
            for (int k = 0; k < nB && k < 64; k++) printf(" %s%04X=%02X", logB[k].out ? "out " : "", logB[k].a, logB[k].v);
            printf("\n  z80lite :");
            for (int k = 0; k < nA && k < 64; k++) printf(" %s%04X=%02X", logA[k].out ? "out " : "", logA[k].a, logA[k].v);
            printf("\n");
        }
        copy_regs(&r);                  // carry on from lib/z80's
        za.total = z80_total(0) - (uint64_t)a_done;
        memcpy(memA, memB, sizeof memA);
        if (++diffs >= 20) {
            printf("  (20 differences: stopping)\n");
            stopped = true;
        }
    }
    nA = nB = 0;
    // the lines, the same for both, seen before the next instruction (not
    // at the start: lib/z80 takes no NMI at cycle 0)
    if (a_at > 1 && rnd() % 997 == 0) {
        z80_set_irq(0, 1);
        z80lite_set_irq(&za, 1);
        vecA = vecB = rnd() % 8 == 0 ? 0xFB : (uint8_t)rnd();     // (IM 0's EI now and then)
        hold = rnd() % 4 == 0;
        irqs++;
    }
    if (hold && rnd() % 64 == 0) {
        z80_set_irq(0, 0);
        z80lite_set_irq(&za, 0);
        hold = false;
    }
    if (a_at > 1 && rnd() % 20011 == 0) {
        z80_set_nmi(0, 1);
        z80_set_nmi(0, 0);
        z80lite_set_nmi(&za, 1);
        z80lite_set_nmi(&za, 0);
        nmis++;
    }
    if (rnd() % 512 == 0) fill((uint16_t)(rnd() & 0xFF00), 256);   // a new page of code now and then
    if (rnd() % 256 == 0) {             // out of any loop: a JP to anywhere, where both fetch next
        const uint16_t to = (uint16_t)rnd();
        memA[r.pc] = memB[r.pc] = 0xC3;
        memA[(uint16_t)(r.pc + 1)] = memB[(uint16_t)(r.pc + 1)] = (uint8_t)to;
        memA[(uint16_t)(r.pc + 2)] = memB[(uint16_t)(r.pc + 2)] = (uint8_t)(to >> 8);
    }
    last_pc = r.pc;
    for (int k = 0; k < 4; k++) last_op[k] = memA[(uint16_t)(last_pc + k)];
    z80_abort(0);
}

static void fuzz(int m, long n, uint64_t seed) {
    mode = m;
    rng = seed * 0x9E3779B97F4A7C15ull + 88172645463325252ull + (uint64_t)m;
    limit = n;
    diffs = irqs = nmis = halts = 0;
    a_at = b_at = 0;
    a_done = 0;
    stopped = woke = hold = false;
    fuzzing = true;
    nA = nB = 0;
    insA = insB = 0;
    fill(0, 0x10000);

    for (int p = 0; p < 256; p++) {
        z80_rd_page[0][p] = mode == 0 || (mode == 1 && p >= 0x40 && p < 0x80) ? NULL : memB + (p << 8);
        z80_wr_page[0][p] = mode == 0 || (mode == 1 && p < 0x80) ? NULL : memB + (p << 8);
    }
    z80_start(0);
    z80_reset(0);
    z80_regs_t r;
    z80_regs(0, &r);
    r.af = (uint16_t)rnd(); r.bc = (uint16_t)rnd(); r.de = (uint16_t)rnd(); r.hl = (uint16_t)rnd();
    r.ix = (uint16_t)rnd(); r.iy = (uint16_t)rnd(); r.sp = (uint16_t)rnd(); r.pc = (uint16_t)rnd();
    r.af2 = (uint16_t)rnd(); r.bc2 = (uint16_t)rnd(); r.de2 = (uint16_t)rnd(); r.hl2 = (uint16_t)rnd();
    r.i = (uint8_t)rnd();
    z80_set_regs(0, &r);
    z80_regs(0, &r);

    z80lite_init(&za);
    za.rd = a_rd; za.wr = a_wr; za.in = a_in; za.out = a_out; za.ack = a_ack;
    if (mode == 1) {
        za.rom = memA; za.rom_base = 0; za.rom_size = 0x4000;
        za.ram = memA + 0x8000; za.ram_base = 0x8000; za.ram_size = 0x8000;
    } else if (mode == 2) {
        za.ram = memA; za.ram_base = 0; za.ram_size = 0x10000;
        za.rom = memA; za.rom_base = 0; za.rom_size = 0x10000;
    }
    copy_regs(&r);

    // z80lite runs slices of 1 to 3000 cycles; lib/z80 follows it
    long seen = -1;
    int still = 0;
    while (!stopped) {
        z80lite_run(&za, 1 + (int)(rnd() % 3000));
        if (a_at == seen && !za.halt && ++still == 100) {   // (interrupts only: the lines change only at an instruction)
            printf("  mode %d instruction %ld: z80lite takes interrupt after interrupt\n", mode, a_at);
            diffs++;
            break;
        }
        if (a_at != seen) still = 0;
        seen = a_at;
        if (stopped || !za.halt || takes_int(&za)) continue;
        // z80lite waits in a HALT with nothing to wake it: lib/z80 runs
        // into the HALT too, then an NMI wakes both
        z80_run(0, 2000);
        if (stopped) break;
        if (!z80_halted(0)) {
            printf("  mode %d instruction %ld: lib/z80 isn't in the HALT\n", mode, a_at);
            diffs++;
            break;
        }
        z80_set_nmi(0, 1);
        z80_set_nmi(0, 0);
        z80lite_set_nmi(&za, 1);
        z80lite_set_nmi(&za, 0);
        woke = true;
        halts++;
    }
    fuzzing = false;
    static const char *const how[] = {"all hooks", "ROM, hooks, RAM", "all RAM"};
    printf("lib/z80lite: fuzz (%s): %ld instructions, %ld IRQs, %ld NMIs, %ld HALTs woken, %ld differences\n", how[mode], a_at - 1,
           irqs, nmis, halts, diffs);
    if (diffs) fails++;
}

// ---- the cases: z80lite alone, hand-assembled code at 0x0000 ---------------------------------
static uint8_t mem[0x10000];
static event_t outs[64];
static int nouts, nacks;
static uint8_t ack_byte, ack_clear;
static uint16_t in_ports[64];
static int nins;
static int abort_at_out;                // an OUT to port 0xAB ends the run

static uint8_t c_in(z80lite_t *z, uint16_t port) {
    (void)z;
    if (nins < 64) in_ports[nins] = port;
    nins++;
    return (uint8_t)(0x40 + nins);      // 0x41, 0x42 ...
}
static void c_out(z80lite_t *z, uint16_t port, uint8_t v) {
    note(outs, &nouts, port, v, 1);
    if (abort_at_out && (port & 0xFF) == 0xAB) z80lite_abort(z);
}
static uint8_t c_ack(z80lite_t *z) {
    nacks++;
    if (ack_clear) z80lite_set_irq(z, 0);
    return ack_byte;
}

static z80lite_t c;
static void load(const uint8_t *p, size_t n) {
    memset(mem, 0, sizeof mem);
    memcpy(mem, p, n);
    z80lite_init(&c);
    c.ram = mem; c.ram_base = 0; c.ram_size = 0x10000;
    c.in = c_in; c.out = c_out; c.ack = c_ack;
    nouts = nins = nacks = 0;
    ack_byte = 0xFF;
    ack_clear = 1;
    abort_at_out = 0;
}
static int run_to(uint16_t pc, int max) {   // steps until PC is pc: the cycles
    int cyc = 0;
    for (int k = 0; k < max && c.pc.w != pc; k++) cyc += z80lite_step(&c);
    return cyc;
}

static void test_ports(void) {
    static const uint8_t p[] = {
        0x3E, 0x12,             // 0000 LD A,12
        0xD3, 0x34,             // 0002 OUT (34),A          port 1234, 12
        0x37,                   // 0004 SCF
        0xDB, 0x56,             // 0005 IN A,(56)           port 1256: 41, flags kept
        0x01, 0x78, 0x9A,       // 0007 LD BC,9A78
        0xED, 0x50,             // 000A IN D,(C)            port 9A78: 42
        0xED, 0x59,             // 000C OUT (C),E
        0xED, 0x70,             // 000E IN (C)              port 9A78: 43, flags only
        0xED, 0x71,             // 0010 OUT (C),0
        0x76,                   // 0012 HALT
    };
    load(p, sizeof p);
    c.de.w = 0x00EE;
    run_to(0x0005, 10);
    CHECK(nouts == 1 && outs[0].a == 0x1234 && outs[0].v == 0x12, "OUT (n),A: %d outs, port %04X = %02X", nouts, outs[0].a, outs[0].v);
    CHECK(c.wz.w == 0x1235, "OUT (n),A: WZ %04X", c.wz.w);
    const uint8_t f = c.af.b.l;
    run_to(0x0007, 10);
    CHECK(nins == 1 && in_ports[0] == 0x1256 && c.af.b.h == 0x41 && c.af.b.l == f, "IN A,(n): port %04X, A %02X F %02X (was %02X)",
          in_ports[0], c.af.b.h, c.af.b.l, f);
    CHECK(c.wz.w == 0x1257, "IN A,(n): WZ %04X", c.wz.w);
    run_to(0x000C, 10);
    CHECK(nins == 2 && in_ports[1] == 0x9A78 && c.de.b.h == 0x42, "IN D,(C): port %04X D %02X", in_ports[1], c.de.b.h);
    CHECK((c.af.b.l & 0xC4) == 0x04 && (c.af.b.l & 0x01), "IN D,(C): flags %02X (P from 42, C kept)", c.af.b.l);
    run_to(0x000E, 10);
    CHECK(nouts == 2 && outs[1].a == 0x9A78 && outs[1].v == 0xEE, "OUT (C),E: port %04X = %02X", outs[1].a, outs[1].v);
    const uint8_t a = c.af.b.h;
    int cyc = z80lite_step(&c);
    CHECK(nins == 3 && c.af.b.h == a && (c.af.b.l & 0x28) == (0x43 & 0x28) && cyc == 12, "IN (C): A %02X F %02X, %d cycles", c.af.b.h,
          c.af.b.l, cyc);
    z80lite_step(&c);
    CHECK(nouts == 3 && outs[2].v == 0x00, "OUT (C),0: %02X", outs[2].v);

    // no hooks: IN gives FF, OUT goes nowhere
    load(p, sizeof p);
    c.in = NULL; c.out = NULL;
    run_to(0x0007, 10);
    CHECK(c.af.b.h == 0xFF, "IN with no hook: %02X", c.af.b.h);
}

static void test_block_io(void) {
    static const uint8_t p[] = {
        0x21, 0x00, 0x40,       // 0000 LD HL,4000
        0x01, 0x10, 0x03,       // 0003 LD BC,0310        B 3, C 10
        0xED, 0xB2,             // 0006 INIR
        0x21, 0x00, 0x50,       // 0008 LD HL,5000
        0x01, 0x20, 0x02,       // 000B LD BC,0220
        0xED, 0xB3,             // 000E OTIR
        0x21, 0x01, 0x50,       // 0010 LD HL,5001
        0x01, 0x30, 0x02,       // 0013 LD BC,0230
        0xED, 0xBB,             // 0016 OTDR
        0x76,                   // 0018 HALT
    };
    load(p, sizeof p);
    mem[0x5000] = 0xA1;
    mem[0x5001] = 0xB2;
    run_to(0x0006, 10);
    int cyc = 0;
    cyc += z80lite_step(&c);
    CHECK(cyc == 21 && c.pc.w == 0x0006, "INIR repeating: %d cycles, PC %04X", cyc, c.pc.w);
    cyc += run_to(0x0008, 10);
    CHECK(cyc == 21 + 21 + 16, "INIR: %d cycles", cyc);
    CHECK(nins == 3 && in_ports[0] == 0x0310 && in_ports[1] == 0x0210 && in_ports[2] == 0x0110,
          "INIR: ports %04X %04X %04X (the port is BC before B counts down)", in_ports[0], in_ports[1], in_ports[2]);
    CHECK(mem[0x4000] == 0x41 && mem[0x4001] == 0x42 && mem[0x4002] == 0x43 && c.hl.w == 0x4003 && c.bc.b.h == 0,
          "INIR: memory %02X %02X %02X, HL %04X B %02X", mem[0x4000], mem[0x4001], mem[0x4002], c.hl.w, c.bc.b.h);
    CHECK(c.af.b.l & 0x40, "INIR: Z at the end (F %02X)", c.af.b.l);
    run_to(0x0010, 20);
    CHECK(nouts == 2 && outs[0].a == 0x0120 && outs[0].v == 0xA1 && outs[1].a == 0x0020 && outs[1].v == 0xB2,
          "OTIR: %04X=%02X %04X=%02X (the port is BC after B counts down)", outs[0].a, outs[0].v, outs[1].a, outs[1].v);
    run_to(0x0018, 20);
    CHECK(nouts == 4 && outs[2].a == 0x0130 && outs[2].v == 0xB2 && outs[3].a == 0x0030 && outs[3].v == 0xA1 && c.hl.w == 0x4FFF,
          "OTDR: %04X=%02X %04X=%02X HL %04X", outs[2].a, outs[2].v, outs[3].a, outs[3].v, c.hl.w);
}

// an interrupt with the code at 0000 running NOPs from 0100, its handlers RET'ing back
static void irq_setup(int im) {
    static uint8_t p[0x200];
    memset(p, 0, sizeof p);
    p[0x0000] = 0x31; p[0x0001] = 0x00; p[0x0002] = 0xF0;   // LD SP,F000
    p[0x0003] = 0x3E; p[0x0004] = 0x01;                     // LD A,01
    p[0x0005] = 0xED; p[0x0006] = 0x47;                     // LD I,A
    p[0x0007] = 0xED; p[0x0008] = im == 0 ? 0x46 : im == 1 ? 0x56 : 0x5E;   // IM n
    p[0x0009] = 0xFB;                                       // EI
    p[0x000A] = 0xC3; p[0x000B] = 0x00; p[0x000C] = 0x01;   // JP 0100
    load(p, sizeof p);
    for (int k = 0x10; k <= 0x38; k += 8) mem[k] = 0xC9;    // RET at each RST
    mem[0x0066] = 0xED; mem[0x0067] = 0x45;                 // RETN
    mem[0x0090] = 0xC9;                                     // IM 2's handler
    mem[0x0134] = 0x90; mem[0x0135] = 0x00;                 // the vector at 0134 (I 01, byte 34)
    run_to(0x0100, 20);
}

static void test_interrupts(void) {
    // IM 1: RST 38h, 13 cycles; the line stays until cleared (here by the ack)
    irq_setup(1);
    mem[0x0038] = 0xC9;
    z80lite_set_irq(&c, 1);
    int cyc = z80lite_step(&c);
    CHECK(cyc == 13 && c.pc.w == 0x0038 && !c.iff1 && !c.iff2 && c.sp.w == 0xEFFE, "IM 1: %d cycles, PC %04X iff %d%d SP %04X", cyc,
          c.pc.w, c.iff1, c.iff2, c.sp.w);
    CHECK(nacks == 1 && !c.irq, "IM 1: %d acks, line %d", nacks, c.irq);

    // IM 0: the byte is an RST (D7: RST 10h)
    irq_setup(0);
    ack_byte = 0xD7;
    z80lite_set_irq(&c, 1);
    cyc = z80lite_step(&c);
    CHECK(cyc == 13 && c.pc.w == 0x0010 && mem[0xEFFE] == 0x00 && mem[0xEFFF] == 0x01, "IM 0 RST 10h: %d cycles, PC %04X", cyc, c.pc.w);

    // IM 0 without an ack hook: irq_data
    irq_setup(0);
    c.ack = NULL;
    c.irq_data = 0xEF;                  // RST 28h
    z80lite_set_irq(&c, 1);
    cyc = z80lite_step(&c);
    CHECK(cyc == 13 && c.pc.w == 0x0028, "IM 0 from irq_data: %d cycles, PC %04X", cyc, c.pc.w);
    CHECK(c.irq, "with no ack hook the line stays: %d", c.irq);
    z80lite_set_irq(&c, 0);

    // IM 0's EI: interrupts on, 6 cycles, and one instruction runs before
    // the line (held) is taken again
    irq_setup(0);
    ack_byte = 0xFB;
    ack_clear = 0;
    z80lite_set_irq(&c, 1);
    cyc = z80lite_run(&c, 11);
    CHECK(cyc == 6 + 4 + 6 && c.pc.w == 0x0101 && nacks == 2 && c.iff1, "IM 0 EI: %d cycles, PC %04X, %d acks, iff1 %d", cyc,
          c.pc.w, nacks, c.iff1);
    z80lite_set_irq(&c, 0);

    // IM 2: the vector at I * 256 + the byte, 19 cycles
    irq_setup(2);
    ack_byte = 0x34;
    z80lite_set_irq(&c, 1);
    cyc = z80lite_step(&c);
    CHECK(cyc == 19 && c.pc.w == 0x0090 && c.wz.w == 0x0090, "IM 2: %d cycles, PC %04X", cyc, c.pc.w);

    // a level: an IRQ held while interrupts are off is taken once they're on
    irq_setup(1);
    mem[0x0100] = 0xF3;                 // DI
    mem[0x0103] = 0xFB;                 // EI (0101, 0102: NOPs)
    mem[0x0038] = 0xC9;
    ack_clear = 0;
    z80lite_step(&c);                   // DI
    z80lite_set_irq(&c, 1);
    z80lite_step(&c);
    z80lite_step(&c);
    CHECK(c.pc.w == 0x0103 && nacks == 0, "held off by DI: PC %04X, %d acks", c.pc.w, nacks);
    z80lite_step(&c);                   // EI
    z80lite_step(&c);                   // the NOP after it runs first
    CHECK(c.pc.w == 0x0105 && nacks == 0, "the EI delay: PC %04X, %d acks", c.pc.w, nacks);
    z80lite_step(&c);
    CHECK(c.pc.w == 0x0038 && nacks == 1, "after EI's next instruction: PC %04X, %d acks", c.pc.w, nacks);
    z80lite_set_irq(&c, 0);

    // EI then DI: no interrupt between them
    irq_setup(1);
    c.iff1 = c.iff2 = 0;
    mem[0x0100] = 0xFB;                 // EI
    mem[0x0101] = 0xF3;                 // DI
    z80lite_set_irq(&c, 1);
    run_to(0x0104, 5);
    CHECK(nacks == 0, "EI; DI: %d acks", nacks);
    z80lite_set_irq(&c, 0);

    // an NMI straight after EI is taken, and ends the EI's delay: an IRQ
    // held is taken as soon as RETN lets it in (one run, so nothing looks
    // at the lines in between)
    irq_setup(1);
    c.iff1 = c.iff2 = 0;
    mem[0x0100] = 0xFB;                 // EI
    mem[0x0038] = 0xC9;
    z80lite_set_irq(&c, 1);
    z80lite_step(&c);
    z80lite_set_nmi(&c, 1);
    cyc = z80lite_run(&c, 26);
    CHECK(cyc == 11 + 14 + 13 && c.pc.w == 0x0038 && nacks == 1, "EI, NMI, RETN, IRQ: %d cycles, PC %04X, %d acks", cyc, c.pc.w, nacks);
    z80lite_set_nmi(&c, 0);

    // NMI: on a rise only, to 0066 in 11 cycles; IFF2 keeps IFF1 for RETN
    irq_setup(1);
    z80lite_set_nmi(&c, 1);
    cyc = z80lite_step(&c);
    CHECK(cyc == 11 && c.pc.w == 0x0066 && !c.iff1 && c.iff2, "NMI: %d cycles, PC %04X iff %d%d", cyc, c.pc.w, c.iff1, c.iff2);
    z80lite_step(&c);                   // RETN
    CHECK(c.pc.w == 0x0100 && c.iff1, "RETN: PC %04X iff1 %d", c.pc.w, c.iff1);
    z80lite_step(&c);
    CHECK(c.pc.w == 0x0101, "a held NMI line is taken once: PC %04X", c.pc.w);
    z80lite_set_nmi(&c, 0);
    z80lite_set_nmi(&c, 1);
    z80lite_step(&c);
    CHECK(c.pc.w == 0x0066, "a second rise: PC %04X", c.pc.w);
}

static void test_halt(void) {
    irq_setup(1);
    mem[0x0100] = 0x76;                 // HALT
    mem[0x0038] = 0xC9;
    z80lite_step(&c);
    CHECK(c.halt && c.pc.w == 0x0101, "HALT: halt %d PC %04X", c.halt, c.pc.w);
    const uint8_t r = c.r;
    int ran = z80lite_run(&c, 1001);
    CHECK(ran == 1004 && c.halt && (uint8_t)(c.r - r) == 251, "halted: ran %d (whole NOPs), R +%d", ran, (uint8_t)(c.r - r));
    z80lite_set_irq(&c, 1);
    int cyc = z80lite_step(&c);
    CHECK(cyc == 13 && !c.halt && c.pc.w == 0x0038 && mem[0xEFFE] == 0x01 && mem[0xEFFF] == 0x01,
          "an IRQ wakes it: %d cycles, PC %04X, back to %02X%02X", cyc, c.pc.w, mem[0xEFFF], mem[0xEFFE]);
}

static void test_windows(void) {
    static uint8_t rom[0x100], ram[0x100], bus[0x10000];
    static const uint8_t p[] = {
        0x31, 0x00, 0x81,       // 0000 LD SP,8100
        0x21, 0x50, 0x00,       // 0003 LD HL,0050
        0x36, 0x77,             // 0006 LD (HL),77       a write to the ROM window: the hook's
        0x7E,                   // 0008 LD A,(HL)        still the ROM's
        0x32, 0x10, 0x80,       // 0009 LD (8010),A      the RAM window
        0x3A, 0x00, 0x40,       // 000C LD A,(4000)      the hook
        0x32, 0x01, 0x40,       // 000F LD (4001),A
        0x3A, 0x00, 0xC0,       // 0012 LD A,(C000)      no window, no hook
        0xCD, 0x00, 0x80,       // 0015 CALL 8000        code in RAM
        0x76,                   // 0018 HALT
    };
    memset(rom, 0, sizeof rom);
    memcpy(rom, p, sizeof p);
    rom[0x50] = 0x5A;
    memset(ram, 0, sizeof ram);
    ram[0x00] = 0x3E; ram[0x01] = 0x99;  // 8000 LD A,99
    ram[0x02] = 0xC9;                    // 8002 RET
    memset(bus, 0, sizeof bus);
    bus[0x4000] = 0x3C;
    z80lite_init(&c);
    c.rom = rom; c.rom_base = 0; c.rom_size = sizeof rom;
    c.ram = ram; c.ram_base = 0x8000; c.ram_size = sizeof ram;
    nA = 0;
    c.rd = a_rd;                        // (memA: the bus)
    c.wr = a_wr;
    mode = 0;
    memcpy(memA, bus, sizeof bus);
    run_to(0x0008, 10);
    CHECK(nA == 1 && logA[0].a == 0x0050 && logA[0].v == 0x77 && rom[0x50] == 0x5A, "a ROM write to the hook: %d, %04X=%02X, ROM %02X",
          nA, logA[0].a, logA[0].v, rom[0x50]);
    run_to(0x000C, 10);
    CHECK(c.af.b.h == 0x5A && ram[0x10] == 0x5A && nA == 1, "ROM read %02X, RAM written %02X, hook writes %d", c.af.b.h, ram[0x10], nA);
    run_to(0x0012, 10);
    CHECK(c.af.b.h == 0x3C && nA == 2 && logA[1].a == 0x4001 && logA[1].v == 0x3C, "the hook: read %02X, wrote %04X=%02X", c.af.b.h,
          logA[1].a, logA[1].v);
    c.rd = NULL;
    run_to(0x0015, 10);
    CHECK(c.af.b.h == 0xFF, "no window and no hook: %02X", c.af.b.h);
    run_to(0x0018, 10);
    CHECK(c.af.b.h == 0x99 && c.sp.w == 0x8100 && ram[0xFE] == 0x18 && ram[0xFF] == 0x00, "code in RAM: A %02X SP %04X", c.af.b.h, c.sp.w);
}

// two CPUs on the same hooks, told apart by user; one's OUT ends its run
static void test_two_cpus(void) {
    static uint8_t m0[0x100], m1[0x100];
    memset(m0, 0, sizeof m0);
    memset(m1, 0, sizeof m1);
    // 0000 INC A; OUT (AB),A; JR 0000
    static const uint8_t p[] = {0x3C, 0xD3, 0xAB, 0x18, 0xFB};
    memcpy(m0, p, sizeof p);
    memcpy(m1, p, sizeof p);
    m1[0] = 0x05;                       // DEC B instead
    z80lite_t z0, z1;
    z80lite_init(&z0);
    z80lite_init(&z1);
    z0.ram = m0; z0.ram_size = sizeof m0; z0.rom = m0; z0.rom_size = sizeof m0;
    z1.ram = m1; z1.ram_size = sizeof m1;
    z0.out = z1.out = c_out;
    z0.user = &z0; z1.user = &z1;
    nouts = 0;
    abort_at_out = 0;
    int r0 = z80lite_run(&z0, 100), r1 = z80lite_run(&z1, 100);
    CHECK(r0 >= 100 && r0 < 120 && r1 >= 100 && r1 < 120, "two CPUs ran %d and %d", r0, r1);
    CHECK(z0.af.b.h == (uint8_t)(0xFF + 4) && z1.bc.b.h == (uint8_t)(0 - 4) && z1.af.b.h == 0xFF, "two CPUs apart: A %02X, B %02X A %02X",
          z0.af.b.h, z1.bc.b.h, z1.af.b.h);
    CHECK(z0.total == (uint64_t)r0 && z1.total == (uint64_t)r1, "totals %llu %llu", (unsigned long long)z0.total, (unsigned long long)z1.total);
    abort_at_out = 1;
    const int r = z80lite_run(&z0, 1000);
    CHECK(r == 4 + 11 && z0.pc.w == 0x0003, "z80lite_abort: ran %d, PC %04X", r, z0.pc.w);
}

// the idle skip: a sound driver waiting on a flag that only the board changes
static void test_idle(void) {
    static const uint8_t p[] = {
        0x31, 0x00, 0xF0,       // 0000 LD SP,F000
        0x21, 0x00, 0x00,       // 0003 LD HL,0000
        0x3A, 0x00, 0x80,       // 0006 wait: LD A,(8000)
        0x22, 0x02, 0x80,       // 0009 LD (8002),HL      (the same value each time: not a change)
        0xB7,                   // 000C OR A
        0x28, 0xF7,             // 000D JR Z,wait
        0x23,                   // 000F INC HL
        0xAF,                   // 0010 XOR A
        0x32, 0x00, 0x80,       // 0011 LD (8000),A
        0x18, 0xF0,             // 0014 JR wait
    };
    for (int skip = 0; skip < 2; skip++) {
        load(p, sizeof p);
        c.idle_skip = skip;
        int ran = z80lite_run(&c, 100000);
        CHECK(ran >= 100000 && ran < 100020, "idle %d: ran %d", skip, ran);
        CHECK(skip ? c.idle_cycles > 99000 : c.idle_cycles == 0, "idle %d: %u cycles let pass", skip, (unsigned)c.idle_cycles);
        CHECK(c.pc.w >= 0x0006 && c.pc.w <= 0x000F && c.hl.w == 0, "idle %d: waiting at %04X, HL %04X", skip, c.pc.w, c.hl.w);
        mem[0x8000] = 1;                // the board's command
        ran = z80lite_run(&c, 1000);
        CHECK(c.hl.w == 1 && mem[0x8000] == 0 && mem[0x8002] == 1, "idle %d: the command taken: HL %04X", skip, c.hl.w);
    }
    // a loop that counts isn't idle
    static const uint8_t q[] = {0x23, 0x18, 0xFD};     // 0000 INC HL; JR 0000
    load(q, sizeof q);
    c.idle_skip = true;
    z80lite_run(&c, 1800);
    CHECK(c.idle_cycles == 0 && c.hl.w == 100, "counting: %u let pass, HL %u", (unsigned)c.idle_cycles, c.hl.w);
    // an interrupt during an idle slice ends the wait
    load(p, sizeof p);
    c.idle_skip = true;
    c.ack = c_ack;
    static const uint8_t h[] = {0x3E, 0x01, 0x32, 0x00, 0x80, 0xC9};  // RST 38h: LD A,1; LD (8000),A; RET
    memcpy(mem + 0x38, h, sizeof h);
    c.im = 1; c.iff1 = c.iff2 = 1;
    z80lite_run(&c, 5000);
    z80lite_set_irq(&c, 1);
    z80lite_run(&c, 200);
    CHECK(nacks == 1 && c.hl.w == 1, "an IRQ in the wait: %d acks, HL %04X", nacks, c.hl.w);
}

// Q: SCF and CCF take X and Y from A, or from A and F when the last
// instruction left the flags alone
static void test_q(void) {
    static const uint8_t p[] = {
        0x3E, 0x00,             // 0000 LD A,00
        0xF5,                   // 0002 PUSH AF
        0xF1,                   // 0003 POP AF           F 28 from the stack, A 00
        0x37,                   // 0004 SCF              after POP AF (flags left alone): X, Y from F | A
        0xB7,                   // 0005 OR A             F 44
        0x37,                   // 0006 SCF              after OR A: from A
        0x76,
    };
    load(p, sizeof p);
    c.sp.w = 0xF000;
    run_to(0x0002, 3);
    c.af.b.l = 0x28;
    run_to(0x0005, 5);
    CHECK((c.af.b.l & 0x28) == 0x28, "SCF after POP AF: F %02X", c.af.b.l);
    run_to(0x0007, 5);
    CHECK((c.af.b.l & 0x28) == 0x00, "SCF after OR A: F %02X", c.af.b.l);
}

int main(int argc, char **argv) {
    const long n = argc > 1 ? atol(argv[1]) : 1000000;
    const uint64_t seed = argc > 2 ? (uint64_t)atoll(argv[2]) : 1;
    test_ports();
    test_block_io();
    test_interrupts();
    test_halt();
    test_windows();
    test_two_cpus();
    test_idle();
    test_q();
    for (int m = 0; m < 3; m++) fuzz(m, n, seed);
    if (fails) {
        printf("lib/z80lite: %d failed\n", fails);
        return 1;
    }
    printf("lib/z80lite: ok\n");
    return 0;
}
