// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
//
// z80.h -- the Zilog Z80 as MAME 0.288 emulates it (src/devices/cpu/z80:
// z80.cpp, z80.h and z80.lst), in C, for one Z80 or several sharing one
// copy of the code. An instruction stops at any of MAME's steps when its
// cycles run out and carries on there in the next timeslice, as MAME's
// does, so a board built on it can match MAME cycle for cycle.
//
// z80.c is a template: the board's own .c file defines its connections
// (below) and includes it, so every memory access inlines:
//
//   #include "z80/z80.h"
//   #define Z80_CPUS 2                          // required
//   #define Z80_RD(cpu, a)     board_rd(cpu, a)  // a read from a NULL page
//   #define Z80_WR(cpu, a, d)  board_wr(cpu, a, d)
//   #define Z80_IN(cpu, port)  board_in(cpu, port)
//   ...
//   #include "z80/z80.c"
//
// Each CPU (cpu = 0 .. Z80_CPUS-1) has its page tables, z80_rd_page[cpu]
// and z80_wr_page[cpu]: 256 pages of 256 bytes, filled in by the board. An
// access to a page that is set goes straight to it; a NULL page calls the
// board's Z80_RD or Z80_WR. lib/z80/README.md lists every connection.
//
// The cycle functions (z80_done, z80_abort, z80_adjust_icount,
// z80_total) work from the board's handlers: Z80_RD, Z80_WR, Z80_IN,
// Z80_OUT, Z80_IRQ_VECTOR, Z80_IRQACK, Z80_RETI, Z80_BUSACK and Z80_TRACE
// see the cycle count as it is at the access, and a change they make to
// it holds. The inline connections (the wait states, Z80_OPCODE,
// Z80_WR_PAGED) must not call them.
//
// MAME's license (BSD-3-Clause) applies: see THIRD-PARTY.md.
#ifndef Z80_H
#define Z80_H

#include <stdbool.h>
#include <stdint.h>

// The page tables: z80_rd_page[cpu][a >> 8] + (a & 0xff) is read, and
// z80_wr_page[cpu][a >> 8] + (a & 0xff) written; NULL calls the board.
// With Z80_OPCODES, opcode fetches (M1) read z80_op_page (MAME's
// AS_OPCODES: decrypted opcodes), NULL calling Z80_RD_OP.
extern const uint8_t *z80_rd_page[][256];
extern uint8_t *z80_wr_page[][256];
extern const uint8_t *z80_op_page[][256];

void z80_start(int cpu);                    // device_start: power on
void z80_reset(int cpu);                    // device_reset (and its total cycles to 0, as MAME's reset does)
int z80_run(int cpu, int cycles);           // execute_run for a timeslice: the cycles run
void z80_abort(int cpu);                    // abort_timeslice: the rest of the timeslice taken back
void z80_adjust_icount(int cpu, int delta); // adjust_icount: from a handler, delta < 0 takes cycles (wait states)
void z80_eat_cycles(int cpu, int cycles);   // eat_cycles: run the cycles without running (counted as run)
int z80_done(int cpu);                      // cycles run so far in this timeslice (cycles_running - icount)
int z80_icount(int cpu);                    // the cycles left in this timeslice
uint64_t z80_total(int cpu);                // total cycles since its reset, this timeslice's so far too
void z80_set_irq(int cpu, int state);       // INPUT_LINE_IRQ0: 1 ASSERT_LINE, 0 CLEAR_LINE
void z80_set_nmi(int cpu, int state);       // INPUT_LINE_NMI (pending on a rising edge once it has run)
void z80_set_busreq(int cpu, int state);    // Z80_INPUT_LINE_BUSREQ
bool z80_busack(int cpu);                   // BUSACK (the bus given up)
bool z80_halted(int cpu);                   // in a HALT

typedef struct {
	uint16_t pc, sp, af, bc, de, hl, ix, iy, af2, bc2, de2, hl2, wz;
	uint8_t r, i, im, iff1, iff2, halt;
} z80_regs_t;
void z80_regs(int cpu, z80_regs_t *r);          // as MAME's state interface gives them (F from the flags, R from r and r2)
void z80_set_regs(int cpu, const z80_regs_t *r); // between instructions (not halt: leave it with z80_reset or an interrupt)

#endif
