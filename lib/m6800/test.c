// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// test.c -- a smoke test for lib/m6800: hand-assembled instructions in a 64K
// RAM, checking the registers, the flags, MAME's cycle counts and its
// interrupt timing, and that M6800_LOOP_SKIP leaves the CPU as running every
// turn does. No ROM. Exits 1 on any failure. README.md has the command.
#include <stdio.h>
#include <stdlib.h>

static unsigned char mem[0x10000];
static int aborts;                      // a write to $FF00 ends the timeslice

static void wr(void *cpu, unsigned a, unsigned char v);
#define M6800_RD(a) mem[a]
#define M6800_WR(a, v) wr(c, a, v)
#include "m6800/m6800.c"

// a second copy, with the delay-loop shortcut
#define M6800_NAME(x) fast_##x
#define M6800_RD(a) mem[a]
#define M6800_WR(a, v) wr(c, a, v)
#define M6800_LOOP_SKIP
#include "m6800/m6800.c"

static void wr(void *cpu, unsigned a, unsigned char v) {
    mem[a & 0xFFFF] = v;
    if (a == 0xFF00) { aborts++; m6800_abort(cpu); }
}

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static m6800_t cpu;

// the program at $1000, the reset vector pointing at it
static void load(const unsigned char *p, unsigned n) {
    for (unsigned i = 0; i < n; i++) mem[0x1000 + i] = p[i];
    mem[0xFFFE] = 0x10; mem[0xFFFF] = 0x00;
    m6800_init(&cpu);
    m6800_reset(&cpu);
}
// a one-cycle timeslice: one instruction (or an interrupt's entry and one more)
static int one(void) { return m6800_run(&cpu, 1); }
static void expect_cycles(const char *what, int want) {
    int got = one();
    CHECK(got == want, "%s: %d cycles, want %d", what, got, want);
}

int main(void) {
    CHECK(m6800_cycles_per_second(3579545, 4) == 894887, "a 6808 on 3.579545 MHz: %u", (unsigned)m6800_cycles_per_second(3579545, 4));

    // ---- reset, loads, adds, CPX, a store's flags
    {
        static const unsigned char p[] = {
            0x01,                   // NOP
            0x86, 0x7F,             // LDAA #$7F
            0x8B, 0x01,             // ADDA #$01   -> $80: N V H
            0xC6, 0xFF,             // LDAB #$FF
            0xCB, 0x01,             // ADDB #$01   -> $00: Z C H
            0xCE, 0x80, 0x00,       // LDX #$8000
            0x8C, 0x7F, 0xFF,       // CPX #$7FFF  (6800: V from the high bytes)
            0xB7, 0x20, 0x00,       // STAA $2000
            0x87, 0x55,             // STAA # (MAME: writes A over the next byte)
        };
        load(p, sizeof p);
        CHECK(cpu.pc == 0x1000 && cpu.cc == 0xD0, "reset PC %04X CC %02X", cpu.pc, cpu.cc);
        expect_cycles("NOP", 2);
        expect_cycles("LDAA #", 2);
        expect_cycles("ADDA #", 2);
        CHECK(cpu.a == 0x80 && (cpu.cc & 0x2F) == (M6800_CC_N | M6800_CC_V | M6800_CC_H), "ADDA A %02X CC %02X", cpu.a, cpu.cc);
        expect_cycles("LDAB #", 2);
        expect_cycles("ADDB #", 2);
        CHECK(cpu.b == 0 && (cpu.cc & 0x2F) == (M6800_CC_Z | M6800_CC_C | M6800_CC_H), "ADDB CC %02X", cpu.cc);
        expect_cycles("LDX #", 3);
        expect_cycles("CPX #", 3);
        CHECK((cpu.cc & (M6800_CC_N | M6800_CC_Z | M6800_CC_V)) == M6800_CC_V, "CPX CC %02X", cpu.cc);
        expect_cycles("STAA ext", 5);
        CHECK(mem[0x2000] == 0x80 && (cpu.cc & M6800_CC_N), "STAA wrote %02X", mem[0x2000]);
        expect_cycles("STAA #", 3);
        CHECK(mem[0x1013] == 0x80 && cpu.pc == 0x1014, "STAA # wrote %02X, PC %04X", mem[0x1013], cpu.pc);
    }
    // ---- the stack and subroutines
    {
        static const unsigned char p[] = {
            0x8E, 0x01, 0xFF,       // LDS #$01FF
            0x86, 0x11, 0x36,       // LDAA #$11, PSHA
            0x4F, 0x32,             // CLRA, PULA
            0xBD, 0x10, 0x20,       // JSR $1020
            0x27, 0x02,             // BEQ +2 (taken: Z is still set from CLRA)
        };
        load(p, sizeof p);
        mem[0x1020] = 0x39;         // RTS
        expect_cycles("LDS #", 3);
        one();
        expect_cycles("PSHA", 4);
        CHECK(cpu.s == 0x01FE && mem[0x01FF] == 0x11, "PSHA S %04X", cpu.s);
        one();
        expect_cycles("PULA", 4);
        CHECK(cpu.a == 0x11 && cpu.s == 0x01FF, "PULA A %02X", cpu.a);
        expect_cycles("JSR ext", 9);
        CHECK(cpu.pc == 0x1020 && cpu.s == 0x01FD && mem[0x01FE] == 0x10 && mem[0x01FF] == 0x0B, "JSR PC %04X S %04X", cpu.pc, cpu.s);
        expect_cycles("RTS", 5);
        expect_cycles("BEQ taken", 4);
        CHECK(cpu.pc == 0x100F, "BEQ PC %04X", cpu.pc);
    }
    // ---- undefined opcodes: 4 cycles, 1, 2 or 3 bytes, no memory touched
    {
        static const unsigned char p[] = {0x02, 0x61, 0x00, 0x71, 0x00, 0x00, 0x01};
        load(p, sizeof p);
        expect_cycles("$02", 4);
        CHECK(cpu.pc == 0x1001, "$02 PC %04X", cpu.pc);
        expect_cycles("$61", 4);
        CHECK(cpu.pc == 0x1003, "$61 PC %04X", cpu.pc);
        expect_cycles("$71", 4);
        CHECK(cpu.pc == 0x1006, "$71 PC %04X", cpu.pc);
    }
    // ---- IRQ: taken when a slice starts, then one instruction; TAP and CLI run the next one first
    {
        static const unsigned char p[] = {
            0x8E, 0x01, 0xFF,       // LDS #$01FF
            0x4F,                   // CLRA
            0x06,                   // TAP: CC = 0 (I clear); the NOP runs inside, then the IRQ
            0x01,                   // NOP
            0x0F,                   // SEI
            0x0E,                   // CLI: I was set, so the NOP runs inside, then the IRQ
            0x01,                   // NOP
            0x01,
        };
        load(p, sizeof p);
        mem[0xFFF8] = 0x30; mem[0xFFF9] = 0x00;     // IRQ -> $3000
        mem[0x3000] = 0x0F;                         // SEI
        mem[0x3001] = 0x3B;                         // RTI
        one(); one();
        m6800_set_irq(&cpu, 1);
        expect_cycles("TAP + NOP + IRQ entry", 2 + 2 + 12);
        CHECK(cpu.pc == 0x3000 && cpu.s == 0x01FF - 7 && (cpu.cc & M6800_CC_I), "TAP IRQ PC %04X S %04X", cpu.pc, cpu.s);
        CHECK(mem[0x01FE] == 0x10 && mem[0x01FF] == 0x06, "TAP IRQ stacked PC %02X%02X", mem[0x01FE], mem[0x01FF]);
        expect_cycles("SEI", 2);
        // RTI pulls CC with I clear and the line still high: the IRQ again, at once
        expect_cycles("RTI + IRQ entry", 10 + 12);
        CHECK(cpu.pc == 0x3000, "RTI IRQ PC %04X", cpu.pc);
        m6800_set_irq(&cpu, 0);
        one();
        expect_cycles("RTI", 10);
        CHECK(cpu.pc == 0x1006, "back PC %04X", cpu.pc);
        one();                                      // SEI
        m6800_set_irq(&cpu, 1);
        expect_cycles("CLI + NOP + IRQ entry", 2 + 2 + 12);
        CHECK(mem[0x01FE] == 0x10 && mem[0x01FF] == 0x09, "CLI IRQ stacked PC %02X%02X", mem[0x01FE], mem[0x01FF]);
        m6800_set_irq(&cpu, 0);
        // a slice that starts with IRQ and I clear: the entry, then one instruction
        one(); one();                               // SEI, RTI (I clear again)
        m6800_set_irq(&cpu, 1);
        expect_cycles("IRQ entry + SEI", 12 + 2);
        m6800_set_irq(&cpu, 0);
    }
    // ---- WAI: 9 cycles, the slice's rest while waiting, 4 to come out
    {
        static const unsigned char p[] = {
            0x8E, 0x01, 0xFF,       // LDS #$01FF
            0x0E,                   // CLI (I was set: the WAI runs inside it)
            0x3E,                   // WAI
            0x01,
        };
        load(p, sizeof p);
        mem[0xFFF8] = 0x30; mem[0xFFF9] = 0x00;
        mem[0x3000] = 0x01;                         // NOP
        one();
        int n = one();
        CHECK(n == 1 + 9 + 2, "CLI + WAI (the slice's cycle eaten, then 9 and 2): %d cycles", n);
        CHECK(cpu.wai && cpu.s == 0x01FF - 7, "WAI stacked: wai %d S %04X", cpu.wai, cpu.s);
        n = m6800_run(&cpu, 30);
        CHECK(n == 30 && cpu.wai, "waiting: %d cycles", n);
        m6800_set_irq(&cpu, 1);
        expect_cycles("out of WAI + NOP", 4 + 2);
        CHECK(cpu.pc == 0x3001 && !cpu.wai, "WAI IRQ PC %04X", cpu.pc);
        m6800_set_irq(&cpu, 0);
    }
    // ---- NMI: the rising edge
    {
        static const unsigned char p[] = {0x8E, 0x01, 0xFF, 0x01, 0x01};
        load(p, sizeof p);
        mem[0xFFFC] = 0x50; mem[0xFFFD] = 0x00;
        mem[0x5000] = 0x01; mem[0x5001] = 0x01;
        one();
        m6800_set_nmi(&cpu, 1);
        expect_cycles("NMI entry + NOP", 12 + 2);
        CHECK(cpu.pc == 0x5001, "NMI PC %04X", cpu.pc);
        expect_cycles("NOP (the line still high)", 2);
    }
    // ---- timeslices: overrun, abort from a handler, done
    {
        static const unsigned char p[] = {0x01, 0x01, 0x01, 0xB7, 0xFF, 0x00, 0x01, 0x01};
        load(p, sizeof p);
        uint64_t t0 = m6800_now(&cpu);
        int n = m6800_run(&cpu, 5);
        CHECK(n == 6 && m6800_now(&cpu) == t0 + 6, "an overrun: %d cycles", n);
        n = m6800_run(&cpu, 100);
        CHECK(aborts == 1 && n == 5 && cpu.pc == 0x1006, "abort: %d cycles, PC %04X", n, cpu.pc);
        n = m6800_run(&cpu, 3);
        CHECK(n == 4 && m6800_done(&cpu) == 4, "two NOPs: %d, done %d", n, m6800_done(&cpu));
    }
    // ---- the delay-loop shortcut: the same CPU after every slice, at every slice length
    {
        static const unsigned char p[] = {
            0x86, 0x00,             // LDAA #0   (256 turns)
            0x4A, 0x26, 0xFD,       // DECA; BNE
            0xC6, 0x07,             // LDAB #7
            0x5A, 0x26, 0xFD,       // DECB; BNE
            0xCE, 0x01, 0x23,       // LDX #$0123
            0x09, 0x26, 0xFD,       // DEX; BNE
            0x20, 0xEE,             // BRA back to the start
        };
        for (int len = 1; len <= 40; len++) {
            m6800_t a, b;
            load(p, sizeof p);
            a = cpu; b = cpu;
            for (int i = 0; i < 400; i++) {
                int na = m6800_run(&a, len), nb = fast_run(&b, len);
                if (na != nb || a.pc != b.pc || a.a != b.a || a.b != b.b || a.x != b.x || a.cc != b.cc ||
                    m6800_now(&a) != m6800_now(&b)) {
                    CHECK(0, "loop skip, slices of %d, slice %d: %d/%d cycles, PC %04X/%04X A %02X/%02X X %04X/%04X",
                          len, i, na, nb, a.pc, b.pc, a.a, b.a, a.x, b.x);
                    break;
                }
            }
        }
        m6800_t a, b;
        load(p, sizeof p);
        a = cpu; b = cpu;
        int na = m6800_run(&a, 100000), nb = fast_run(&b, 100000);
        CHECK(na == nb && a.pc == b.pc && a.x == b.x && a.cc == b.cc, "loop skip, a long slice: %d/%d", na, nb);
    }
    if (fails) {
        printf("m6800 test: %d failed\n", fails);
        return 1;
    }
    printf("m6800 test: all passed\n");
    return 0;
}
