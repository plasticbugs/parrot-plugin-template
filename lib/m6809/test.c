// license:BSD-3-Clause
// copyright-holders:Nathan Woods
//
// test.c -- a smoke test for lib/m6809: hand-assembled instructions in a 64K
// RAM, each run as a timeslice of one cycle (so exactly one instruction or one
// interrupt's entry), checking the registers, the flags and MAME's cycle
// counts. No ROM. Exits 1 on the first failure. README.md has the command.
#include <stdio.h>
#include <stdlib.h>

static unsigned char mem[0x10000];
static int aborts;                      // a write to $FF00 ends the timeslice

#define M6809_RD(a) mem[a]
#define M6809_WR(a, v) wr(c, a, v)
static void wr(void *cpu, unsigned a, unsigned char v);
#include "m6809/m6809.c"

static void wr(void *cpu, unsigned a, unsigned char v) {
    mem[a & 0xFFFF] = v;
    if (a == 0xFF00) { aborts++; m6809_abort(cpu); }
}

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static m6809_t cpu;

// the program at $1000, the reset vector pointing at it
static void load(const unsigned char *p, unsigned n) {
    for (unsigned i = 0; i < n; i++) mem[0x1000 + i] = p[i];
    mem[0xFFFE] = 0x10; mem[0xFFFF] = 0x00;
    m6809_init(&cpu);
    m6809_reset(&cpu);
}
// one instruction (a one-cycle timeslice); its cycles
static int one(void) { return m6809_run(&cpu, 1); }
static void expect_cycles(const char *what, int want) {
    int got = one();
    CHECK(got == want, "%s: %d cycles, want %d", what, got, want);
}

int main(void) {
    // ---- reset: PC from $FFFE, I and F set, DP 0, the vector's 4 cycles
    {
        static const unsigned char p[] = {0x12};
        load(p, sizeof p);
        CHECK(cpu.pc == 0x1000, "reset PC %04X", cpu.pc);
        CHECK((cpu.cc & (M6809_CC_I | M6809_CC_F)) == (M6809_CC_I | M6809_CC_F), "reset CC %02X", cpu.cc);
        CHECK(m6809_now(&cpu) == 4, "reset cycles %llu", (unsigned long long)m6809_now(&cpu));
        expect_cycles("NOP", 2);
    }
    // ---- loads, adds and their flags
    {
        static const unsigned char p[] = {
            0x86, 0x7F,             // LDA #$7F
            0x8B, 0x01,             // ADDA #$01   -> $80: N V H
            0xC6, 0xFF,             // LDB #$FF
            0xCB, 0x01,             // ADDB #$01   -> $00: Z C H
            0x8E, 0x12, 0x34,       // LDX #$1234
            0x30, 0x05,             // LEAX 5,X    -> $1239
            0xBF, 0x20, 0x00,       // STX $2000
            0x3D,                   // MUL (A=$80, B=$00)
        };
        load(p, sizeof p);
        expect_cycles("LDA #", 2);
        expect_cycles("ADDA #", 2);
        CHECK(cpu.a == 0x80, "ADDA A %02X", cpu.a);
        CHECK((cpu.cc & 0x2F) == (M6809_CC_N | M6809_CC_V | M6809_CC_H), "ADDA CC %02X", cpu.cc);
        expect_cycles("LDB #", 2);
        expect_cycles("ADDB #", 2);
        CHECK(cpu.b == 0 && (cpu.cc & 0x2F) == (M6809_CC_Z | M6809_CC_C | M6809_CC_H), "ADDB B %02X CC %02X", cpu.b, cpu.cc);
        expect_cycles("LDX #", 3);
        expect_cycles("LEAX 5,X", 5);
        CHECK(cpu.x == 0x1239, "LEAX X %04X", cpu.x);
        expect_cycles("STX ext", 6);
        CHECK(mem[0x2000] == 0x12 && mem[0x2001] == 0x39, "STX wrote %02X %02X", mem[0x2000], mem[0x2001]);
        expect_cycles("MUL", 11);
        CHECK(cpu.a == 0 && cpu.b == 0 && (cpu.cc & M6809_CC_Z), "MUL D %02X%02X CC %02X", cpu.a, cpu.b, cpu.cc);
    }
    // ---- the stack, subroutines, branches
    {
        static const unsigned char p[] = {
            0x10, 0xCE, 0x80, 0x00, // LDS #$8000
            0x86, 0x11, 0xC6, 0x22, // LDA #$11, LDB #$22
            0x34, 0x06,             // PSHS A,B
            0x4F, 0x5F,             // CLRA, CLRB
            0x35, 0x06,             // PULS A,B
            0xBD, 0x10, 0x20,       // JSR $1020
            0x27, 0x02,             // BEQ +2 (Z still set from CLRB: taken)
            0x12, 0x12,
            0x10, 0x26, 0x00, 0x00, // LBNE +0 (not taken)
        };
        load(p, sizeof p);
        mem[0x1020] = 0x39;         // RTS
        expect_cycles("LDS #", 4);
        CHECK(cpu.s == 0x8000, "LDS S %04X", cpu.s);
        one(); one();
        expect_cycles("PSHS A,B", 7);
        CHECK(cpu.s == 0x7FFE && mem[0x7FFE] == 0x11 && mem[0x7FFF] == 0x22, "PSHS S %04X", cpu.s);
        one(); one();
        expect_cycles("PULS A,B", 7);
        CHECK(cpu.a == 0x11 && cpu.b == 0x22 && cpu.s == 0x8000, "PULS A %02X B %02X", cpu.a, cpu.b);
        expect_cycles("JSR ext", 8);
        CHECK(cpu.pc == 0x1020 && cpu.s == 0x7FFE, "JSR PC %04X S %04X", cpu.pc, cpu.s);
        expect_cycles("RTS", 5);
        CHECK(cpu.pc == 0x1011, "RTS PC %04X", cpu.pc);
        expect_cycles("BEQ taken", 3);
        CHECK(cpu.pc == 0x1015, "BEQ PC %04X", cpu.pc);
        expect_cycles("LBNE not taken", 5);
    }
    // ---- MAME's own ways: EXG's order, SEX keeps V, XNC, XCLR
    {
        static const unsigned char p[] = {
            0x86, 0x12, 0xC6, 0x34, // LDA #$12, LDB #$34
            0x1E, 0x80,             // EXG A,D: D = $FF12, then A = $34 -> D $3412
            0x1A, 0x02,             // ORCC #V
            0xC6, 0x80,             // LDB #$80 (clears V)
            0x1A, 0x02,             // ORCC #V
            0x1D,                   // SEX -> D $FF80, V stays
            0x1C, 0xFE,             // ANDCC #~C
            0x42,                   // (XNC A: C clear, so NEG) A $FF -> $01
            0x1A, 0x01,             // ORCC #C
            0x4E,                   // (XCLR A: CLR that keeps C)
        };
        load(p, sizeof p);
        one(); one();
        expect_cycles("EXG", 8);
        CHECK(cpu.a == 0x34 && cpu.b == 0x12, "EXG A,D gave A %02X B %02X", cpu.a, cpu.b);
        one(); one(); one();
        expect_cycles("SEX", 2);
        CHECK(cpu.a == 0xFF && cpu.b == 0x80 && (cpu.cc & (M6809_CC_N | M6809_CC_V)) == (M6809_CC_N | M6809_CC_V),
              "SEX D %02X%02X CC %02X", cpu.a, cpu.b, cpu.cc);
        one();
        expect_cycles("XNC A", 2);
        CHECK(cpu.a == 0x01, "XNC A %02X", cpu.a);
        one();
        expect_cycles("XCLR A", 2);
        CHECK(cpu.a == 0 && (cpu.cc & (M6809_CC_Z | M6809_CC_C)) == (M6809_CC_Z | M6809_CC_C), "XCLR CC %02X", cpu.cc);
    }
    // ---- IRQ and FIRQ: their entries' cycles and what they stack
    {
        static const unsigned char p[] = {
            0x10, 0xCE, 0x80, 0x00, // LDS #$8000
            0x1C, 0xAF,             // ANDCC #$AF (I and F clear)
            0x12, 0x12, 0x12,
        };
        load(p, sizeof p);
        mem[0xFFF8] = 0x30; mem[0xFFF9] = 0x00;     // IRQ  -> $3000
        mem[0xFFF6] = 0x40; mem[0xFFF7] = 0x00;     // FIRQ -> $4000
        mem[0x3000] = 0x3B;                         // RTI
        mem[0x4000] = 0x3B;
        one(); one();
        m6809_set_irq(&cpu, 1);
        expect_cycles("IRQ entry", 19);
        CHECK(cpu.pc == 0x3000 && cpu.s == 0x8000 - 12 && (cpu.cc & M6809_CC_I) && (cpu.cc & M6809_CC_E),
              "IRQ PC %04X S %04X CC %02X", cpu.pc, cpu.s, cpu.cc);
        m6809_set_irq(&cpu, 0);
        expect_cycles("RTI (all)", 15);
        CHECK(cpu.pc == 0x1006 && cpu.s == 0x8000 && !(cpu.cc & M6809_CC_I), "RTI PC %04X S %04X", cpu.pc, cpu.s);
        m6809_set_firq(&cpu, 1);
        expect_cycles("FIRQ entry", 10);
        CHECK(cpu.pc == 0x4000 && cpu.s == 0x8000 - 3 && !(cpu.cc & M6809_CC_E), "FIRQ PC %04X S %04X CC %02X", cpu.pc, cpu.s, cpu.cc);
        m6809_set_firq(&cpu, 0);
        expect_cycles("RTI (PC, CC)", 6);
    }
    // ---- NMI: only once S was loaded, on the rising edge
    {
        static const unsigned char p[] = {0x12, 0x10, 0xCE, 0x80, 0x00, 0x12, 0x12};
        load(p, sizeof p);
        mem[0xFFFC] = 0x50; mem[0xFFFD] = 0x00;
        m6809_set_nmi(&cpu, 1);
        expect_cycles("NOP (an NMI before LDS goes unseen)", 2);
        m6809_set_nmi(&cpu, 0);
        expect_cycles("LDS #", 4);
        m6809_set_nmi(&cpu, 1);
        expect_cycles("NMI entry", 19);
        CHECK(cpu.pc == 0x5000 && (cpu.cc & (M6809_CC_I | M6809_CC_F)) == (M6809_CC_I | M6809_CC_F), "NMI PC %04X CC %02X", cpu.pc, cpu.cc);
        mem[0x5000] = 0x12;
        expect_cycles("NOP (the line still high: no second NMI)", 2);
    }
    // ---- CWAI and SYNC
    {
        static const unsigned char p[] = {
            0x10, 0xCE, 0x80, 0x00, // LDS #$8000
            0x3C, 0xEF,             // CWAI #$EF (I clear)
            0x1A, 0x10,             // ORCC #I
            0x13,                   // SYNC (IRQ masked: it ends, and on we go)
            0x12,
        };
        load(p, sizeof p);
        mem[0xFFF8] = 0x30; mem[0xFFF9] = 0x00;
        mem[0x3000] = 0x3B;
        one();
        int n = m6809_run(&cpu, 50);
        CHECK(n == 50 && cpu.wait == 2, "CWAI waits the slice out: %d cycles, wait %d", n, cpu.wait);
        CHECK(cpu.s == 0x8000 - 12, "CWAI stacked: S %04X", cpu.s);
        m6809_set_irq(&cpu, 1);
        expect_cycles("CWAI's IRQ (the vector only)", 4);
        CHECK(cpu.pc == 0x3000, "CWAI IRQ PC %04X", cpu.pc);
        m6809_set_irq(&cpu, 0);
        expect_cycles("RTI", 15);
        expect_cycles("ORCC", 3);
        expect_cycles("SYNC", 2);
        n = m6809_run(&cpu, 20);
        CHECK(n == 20 && cpu.wait == 1, "SYNC waits: %d, wait %d", n, cpu.wait);
        m6809_set_irq(&cpu, 1);
        expect_cycles("SYNC's end (masked IRQ)", 1);
        expect_cycles("NOP after SYNC", 2);
        CHECK(cpu.pc == 0x100A, "after SYNC PC %04X", cpu.pc);
    }
    // ---- timeslices: overrun, abort from a handler, done, adjust_icount
    {
        static const unsigned char p[] = {
            0x12, 0x12, 0x12,       // NOP x3 (6 cycles)
            0xB7, 0xFF, 0x00,       // STA $FF00: the handler aborts
            0x12, 0x12,
        };
        load(p, sizeof p);
        uint64_t t0 = m6809_now(&cpu);
        int n = m6809_run(&cpu, 5);
        CHECK(n == 6 && m6809_now(&cpu) == t0 + 6, "an overrun: %d cycles", n);
        n = m6809_run(&cpu, 100);
        CHECK(aborts == 1 && n == 5 && cpu.pc == 0x1006, "abort: %d cycles, PC %04X", n, cpu.pc);
        CHECK(m6809_now(&cpu) == t0 + 11, "abort: now %llu", (unsigned long long)(m6809_now(&cpu) - t0));
        m6809_adjust_icount(&cpu, 0);
        n = m6809_run(&cpu, 3);
        CHECK(n == 4 && m6809_done(&cpu) == 4, "two NOPs: %d, done %d", n, m6809_done(&cpu));
    }
    if (fails) {
        printf("m6809 test: %d failed\n", fails);
        return 1;
    }
    printf("m6809 test: all passed\n");
    return 0;
}
