// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Parrot Cart contributors
//
// z80lite.h -- a small, fast Z80 that runs whole instructions. It has every
// instruction, the documented flags and the undocumented X and Y bits (with
// the Q rule for SCF and CCF), WZ, R, IM 0, 1 and 2, NMI, HALT, the EI
// delay and the Z80's cycle counts. It was written from Zilog's user manual
// and Sean Young's "The Undocumented Z80 Documented" for the Neo Geo Pocket
// Color plugin's sound CPU, and checked against lib/z80 (MAME 0.288's)
// instruction by instruction. README.md says when to use it and when to use
// lib/z80.
//
// Each CPU is a z80lite_t, so a plugin can have several. The board gives it
// memory and hooks:
//
//   static z80lite_t snd;
//   z80lite_init(&snd);                         // power on
//   snd.rom = rom; snd.rom_base = 0x0000; snd.rom_size = 0x2000;   // read inline
//   snd.ram = ram; snd.ram_base = 0x4000; snd.ram_size = 0x0400;   // read and written inline
//   snd.rd = snd_rd; snd.wr = snd_wr;           // every other address
//   snd.in = snd_in; snd.out = snd_out;         // IN and OUT
//   int ran = z80lite_run(&snd, 3579545 / 60);  // a frame: the cycles it ran
//
// Hooks get the CPU, so CPUs wired alike can share them (z->user is the
// board's). A hook may call z80lite_set_irq, z80lite_set_nmi and
// z80lite_abort on any CPU, but must not run one.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef union {
    uint16_t w;
    struct { uint8_t l, h; } b;
} z80lite_pair_t;

typedef struct z80lite z80lite_t;

struct z80lite {
    // The registers: af.b.h is A and af.b.l is F, and so on. R is r's low 7
    // bits with r7's bit 7 (z80lite_r). A board may read them at any time
    // and set them between runs.
    z80lite_pair_t af, bc, de, hl, ix, iy, sp, pc, af2, bc2, de2, hl2, wz;
    uint8_t i, r, r7, im, iff1, iff2, halt;
    uint8_t ei;                         // an EI just ran: no interrupt before the next instruction
    uint8_t q;                          // 0 if the last instruction set the flags, 0x28 if not (SCF, CCF)
    uint8_t attn;                       // (z80lite_run's: something to look at before the next instruction)

    // The memory. The RAM window is read and written inline: the Z80's
    // address a is ram[a - ram_base] while a - ram_base < ram_size. The ROM
    // window is read inline the same way. Everything else goes to the hooks.
    // A window must not wrap past 0xFFFF (base + size <= 0x10000); size 0 is
    // no window. Opcodes are looked for in the ROM window first and data in
    // the RAM window first. When the code runs from RAM, pointing the ROM
    // window at that RAM as well makes it faster (z80lite never writes
    // through the ROM window, so the writes still go to the RAM window).
    uint8_t *ram;
    uint32_t ram_base, ram_size;
    const uint8_t *rom;
    uint32_t rom_base, rom_size;
    uint8_t (*rd)(z80lite_t *z, uint16_t a);            // other reads; no hook: 0xFF
    void (*wr)(z80lite_t *z, uint16_t a, uint8_t v);    // other writes, the ROM's too; no hook: dropped
    uint8_t (*in)(z80lite_t *z, uint16_t port);         // IN, IN r,(C), INI and the rest; no hook: 0xFF
    void (*out)(z80lite_t *z, uint16_t port, uint8_t v);    // OUT, OUTI and the rest
    // The interrupt's acknowledge: it returns the byte on the data bus, and
    // may clear the line (MAME's HOLD_LINE). No hook: irq_data.
    uint8_t (*ack)(z80lite_t *z);
    void *user;                         // the board's

    // The byte on the data bus when an interrupt is taken (0xFF at power on).
    // IM 0 runs it as an instruction: an RST, or EI (any other byte does
    // nothing, as in MAME). IM 2 reads its vector from I * 256 + irq_data.
    uint8_t irq_data;
    // Off at power on. When it's on, z80lite_run lets the rest of a slice pass
    // at once when the Z80 comes back round a loop with nothing changed. Only
    // safe when nothing it reads changes during a slice (README.md).
    bool idle_skip;

    uint64_t total;                     // cycles run since the last reset
    uint32_t idle_cycles;               // cycles let pass in idle loops (idle_skip)

    // kept by z80lite: the lines, and what the idle skip watches
    uint8_t irq, nmi_line, nmi, stop, idle, idle_miss;
    uint32_t writes, idle_w, idle_pc;
    uint16_t idle_r[12];
};

// Power on: all registers 0 but AF, SP, IX and IY (0xFFFF), no memory, no
// hooks, irq_data 0xFF, the lines clear. Then set the memory and hooks.
void z80lite_init(z80lite_t *z);
// The RESET line: PC, I and R 0, IM 0, interrupts off, out of a HALT, the
// cycle count 0. The other registers, the memory and the hooks stay.
void z80lite_reset(z80lite_t *z);
// Runs whole instructions until at least `cycles` have passed (or less, if a
// hook called z80lite_abort); returns the cycles run. The last instruction
// may overrun: take the extra off the next slice. A HALT with nothing to
// wake it runs its NOPs at once.
int z80lite_run(z80lite_t *z, int cycles);
// One instruction, or the interrupt it takes first: its cycles.
int z80lite_step(z80lite_t *z);
// The INT line, 1 asserted and 0 clear. It is a level: it stays until the
// board clears it (in the ack hook, or when the game acknowledges it).
void z80lite_set_irq(z80lite_t *z, int state);
// The NMI line: an NMI is taken once on each rise.
void z80lite_set_nmi(z80lite_t *z, int state);
// From a hook: z80lite_run returns once this instruction is done.
void z80lite_abort(z80lite_t *z);
// R as the Z80 shows it (LD A,R)
static inline uint8_t z80lite_r(const z80lite_t *z) { return (uint8_t)((z->r & 0x7F) | z->r7); }
