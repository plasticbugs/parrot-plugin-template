// test.c -- lib/z80's smoke test: hand-assembled code on two Z80s sharing
// the core, checked instruction by instruction (each one's address and the
// total cycles as it starts, as MAME's trace shows them) and by registers.
// It checks the board's connections too: a handler that takes cycles
// (z80_adjust_icount) and one that ends the timeslice (z80_abort), the
// interrupt acknowledge (Z80_IRQACK, then Z80_IRQ_VECTOR), NMI, M1 and
// memory wait states, Z80_OPCODE and Z80_WR_PAGED; a handler of one CPU
// resetting the other; and that a CPU run in slices of a few cycles,
// stopping inside instructions and swapped with the other, runs exactly as
// in one long slice.
//
//   cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-missing-field-initializers lib/z80/test.c -o z80test && ./z80test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80.h"

static uint8_t mem[2][0x10000];
static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- the board -----------------------------------------------------------------------------
typedef struct { uint16_t pc; uint64_t total; } step_t;
static step_t steps[2][256];
static int nsteps[2];
static int fetches[2];                  // opcode fetches (Z80_OPCODE)
static int abort_on_out;                // OUT (0x20) ends the timeslice
static int reset_on_out;                // OUT (0x20) resets CPU 1
static int done_at_out = -1;
static int irq_log[8], nirq_log;        // 1 IRQACK, 2 the vector
static int wr_hooked;                   // CPU 0's writes to 0x8000 (sent to the handler)

static void trace(int cpu) {
	z80_regs_t r;
	z80_regs(cpu, &r);
	if (nsteps[cpu] < 256) steps[cpu][nsteps[cpu]++] = (step_t){r.pc, z80_total(cpu)};
}
static uint8_t board_in(int cpu, uint16_t port) {
	if (cpu == 0 && (port & 0xff) == 0x10) {
		z80_adjust_icount(cpu, -5);     // a slow port: 5 more cycles
		return 0x5a;
	}
	return 0xff;
}
static void board_out(int cpu, uint16_t port, uint8_t d) {
	(void)d;
	if (cpu == 0 && (port & 0xff) == 0x20 && abort_on_out) {
		done_at_out = z80_done(cpu);
		z80_abort(cpu);
	}
	if (cpu == 0 && (port & 0xff) == 0x20 && reset_on_out) z80_reset(1);
}
static void board_wr(int cpu, uint16_t a, uint8_t d) {
	if (cpu == 0 && a == 0x8000) wr_hooked++;
	mem[cpu][a] = d;
}
static void irqack(int cpu) { (void)cpu; if (nirq_log < 8) irq_log[nirq_log++] = 1; }
static int irq_vector(int cpu) {
	if (nirq_log < 8) irq_log[nirq_log++] = 2;
	z80_set_irq(cpu, 0);                // as MAME's HOLD_LINE: let go at the acknowledge
	return 0xff;
}

#define Z80_CPUS 2
#define Z80_WR(cpu, a, d) board_wr(cpu, a, d)
#define Z80_IN(cpu, port) board_in(cpu, port)
#define Z80_OUT(cpu, port, d) board_out(cpu, port, d)
#define Z80_IRQACK(cpu) irqack(cpu)
#define Z80_IRQ_VECTOR(cpu) irq_vector(cpu)
#define Z80_TRACE(cpu) trace(cpu)
// CPU 1's bus: 1 wait in each M1, 2 in each access at 0x8000 and up
#define Z80_M1_WAIT(cpu, a) ((cpu) == 1)
#define Z80_RD_WAIT(cpu, a) ((cpu) == 1 && (a) >= 0x8000 ? 2 : 0)
#define Z80_WR_WAIT(cpu, a) ((cpu) == 1 && (a) >= 0x8000 ? 2 : 0)
#define Z80_OPCODE(cpu, a, op) (fetches[cpu]++)
#define Z80_WR_PAGED(cpu, a) ((cpu) != 0 || (a) != 0x8000)
#include "z80.c"

// ---- the programs --------------------------------------------------------------------------
static const uint8_t prog0[] = {
	0x31, 0x00, 0xf0,       // 0000 LD SP,F000h     10
	0x3e, 0x12,             // 0003 LD A,12h         7
	0x06, 0x34,             // 0005 LD B,34h         7
	0x80,                   // 0007 ADD A,B          4
	0x21, 0x00, 0x80,       // 0008 LD HL,8000h     10
	0x77,                   // 000B LD (HL),A        7
	0xdb, 0x10,             // 000C IN A,(10h)      11, +5 by the port
	0xd3, 0x20,             // 000E OUT (20h),A     11
	0x0e, 0x03,             // 0010 LD C,3           7
	0x0d,                   // 0012 DEC C            4
	0x20, 0xfd,             // 0013 JR NZ,0012h     12 / 7
	0xed, 0x56,             // 0015 IM 1             8
	0xfb,                   // 0017 EI               4
	0x00,                   // 0018 NOP              4
	0x18, 0xfe,             // 0019 JR 0019h        12
};
static const uint8_t isr0[] = {0x3c, 0xed, 0x4d};      // 0038 INC A 4; RETI 14 (no EI: IFF1 stays 0)
static const uint8_t nmi0[] = {0xed, 0x45};            // 0066 RETN 14

static const uint8_t prog1[] = {
	0x21, 0x00, 0x80,       // 0000 LD HL,8000h     10 + 1
	0x7e,                   // 0003 LD A,(HL)        7 + 1 + 2
	0x3c,                   // 0004 INC A            4 + 1
	0x77,                   // 0005 LD (HL),A        7 + 1 + 2
	0xdd, 0x21, 0x34, 0x12, // 0006 LD IX,1234h     14 + 2
	0xcb, 0x27,             // 000A SLA A            8 + 2
	0x18, 0xfe,             // 000C JR 000Ch        12 + 1
};
static const step_t want1[] = {
	{0x0000, 0}, {0x0003, 11}, {0x0004, 21}, {0x0005, 26}, {0x0006, 36}, {0x000a, 52}, {0x000c, 62}, {0x000c, 75}, {0x000c, 88},
};

static void setup(void) {
	memset(mem, 0, sizeof mem);
	memcpy(mem[0], prog0, sizeof prog0);
	memcpy(mem[0] + 0x38, isr0, sizeof isr0);
	memcpy(mem[0] + 0x66, nmi0, sizeof nmi0);
	memcpy(mem[1], prog1, sizeof prog1);
	mem[1][0x8000] = 0x40;
	for (int c = 0; c < 2; c++) {
		for (int p = 0; p < 256; p++) {
			z80_rd_page[c][p] = mem[c] + (p << 8);
			z80_wr_page[c][p] = mem[c] + (p << 8);
		}
		z80_start(c);
		z80_reset(c);
	}
	memset(nsteps, 0, sizeof nsteps);
	memset(fetches, 0, sizeof fetches);
	nirq_log = 0;
	wr_hooked = 0;
	abort_on_out = 0;
	reset_on_out = 0;
	done_at_out = -1;
}

static void check_steps(int cpu, const step_t *want, int n, const char *what) {
	CHECK(nsteps[cpu] >= n, "%s: %d instructions traced, want %d", what, nsteps[cpu], n);
	for (int i = 0; i < n && i < nsteps[cpu]; i++)
		CHECK(steps[cpu][i].pc == want[i].pc && steps[cpu][i].total == want[i].total,
		      "%s: instruction %d at %04X, %llu cycles; want %04X, %llu", what, i, steps[cpu][i].pc,
		      (unsigned long long)steps[cpu][i].total, want[i].pc, (unsigned long long)want[i].total);
}

int main(void) {
	// CPU 0: the handlers, an interrupt and an NMI
	setup();
	abort_on_out = 1;
	int ran = z80_run(0, 1000);
	CHECK(ran == 72, "the OUT's abort: ran %d, want 72 (to the OUT's end)", ran);
	CHECK(done_at_out == 68, "z80_done in the OUT's handler: %d, want 68", done_at_out);
	abort_on_out = 0;
	ran = z80_run(0, 138 - 72);         // to the NOP's end
	CHECK(ran == 66, "ran %d, want 66", ran);
	CHECK(z80_total(0) == 138, "total %llu, want 138", (unsigned long long)z80_total(0));
	z80_set_irq(0, 1);
	ran = z80_run(0, 169 + 12 - 138);   // the interrupt, INC A, RETI, one JR
	z80_set_nmi(0, 1);
	ran = z80_run(0, 40);               // the NMI, RETN, JRs
	z80_set_nmi(0, 0);
	static const step_t want0[] = {
		{0x0000, 0}, {0x0003, 10}, {0x0005, 17}, {0x0007, 24}, {0x0008, 28}, {0x000b, 38}, {0x000c, 45},
		{0x000e, 61}, {0x0010, 72}, {0x0012, 79}, {0x0013, 83}, {0x0012, 95}, {0x0013, 99}, {0x0012, 111},
		{0x0013, 115}, {0x0015, 122}, {0x0017, 130}, {0x0018, 134},
		{0x0038, 151}, {0x0039, 155}, {0x0019, 169},            // IM 1: 13 cycles; INC A; RETI
		{0x0066, 192}, {0x001b - 2, 206},                       // NMI after the JR at 169..181: 11 cycles; RETN
	};
	check_steps(0, want0, (int)(sizeof want0 / sizeof want0[0]), "CPU 0");
	CHECK(nirq_log == 2 && irq_log[0] == 1 && irq_log[1] == 2, "the acknowledge: IRQACK then the vector (%d calls)", nirq_log);
	CHECK(wr_hooked == 1 && mem[0][0x8000] == 0x46, "Z80_WR_PAGED: %d writes to the handler, [8000] %02X", wr_hooked, mem[0][0x8000]);
	z80_regs_t r;
	z80_regs(0, &r);
	CHECK(r.af >> 8 == 0x5b && r.bc == 0x3400 && r.hl == 0x8000 && r.sp == 0xf000 && r.im == 1 && r.iff1 == 0,
	      "CPU 0's registers: AF %04X BC %04X HL %04X SP %04X IM %d IFF1 %d", r.af, r.bc, r.hl, r.sp, r.im, r.iff1);

	// CPU 0's OUT resets CPU 1 (part way through its program) while CPU 0 runs
	setup();
	z80_run(1, 40);
	reset_on_out = 1;
	ran = z80_run(0, 138);
	CHECK(ran == 138, "CPU 0 resetting CPU 1: ran %d, want 138", ran);
	check_steps(0, want0, 18, "CPU 0, resetting CPU 1");
	z80_regs(0, &r);
	CHECK(r.af >> 8 == 0x5a && r.bc == 0x3400 && r.hl == 0x8000 && r.sp == 0xf000 && r.im == 1 && r.iff1 == 1,
	      "CPU 0 after resetting CPU 1: AF %04X BC %04X HL %04X SP %04X IM %d IFF1 %d", r.af, r.bc, r.hl, r.sp, r.im, r.iff1);
	z80_regs(1, &r);
	CHECK(r.pc == 0 && z80_total(1) == 0, "CPU 1 reset by CPU 0: PC %04X, total %llu", r.pc, (unsigned long long)z80_total(1));
	nsteps[1] = 0;
	z80_run(1, 90);
	check_steps(1, want1, (int)(sizeof want1 / sizeof want1[0]), "CPU 1 after its reset");

	// CPU 1: wait states, in one long slice
	setup();
	ran = z80_run(1, 90);
	check_steps(1, want1, (int)(sizeof want1 / sizeof want1[0]), "CPU 1, one slice");
	z80_regs(1, &r);
	CHECK(r.af >> 8 == 0x82 && r.hl == 0x8000 && r.ix == 0x1234 && mem[1][0x8000] == 0x41,
	      "CPU 1's registers: AF %04X HL %04X IX %04X [8000] %02X", r.af, r.hl, r.ix, mem[1][0x8000]);
	const uint16_t af_long = r.af;
	const uint64_t total_long = z80_total(1);
	const int fetches_long = fetches[1];

	// the same in slices of 1 to 5 cycles, swapped with CPU 0 at each
	setup();
	uint64_t want_total = total_long;
	for (int k = 0; z80_total(1) < want_total; k++) {
		int n = 1 + k % 5;
		if (z80_total(1) + (uint64_t)n > want_total) n = (int)(want_total - z80_total(1));
		z80_run(1, n);
		z80_run(0, 3);
	}
	check_steps(1, want1, (int)(sizeof want1 / sizeof want1[0]), "CPU 1, slices of 1-5");
	z80_regs(1, &r);
	CHECK(r.af == af_long && r.ix == 0x1234 && mem[1][0x8000] == 0x41, "CPU 1 in slices: AF %04X (want %04X) IX %04X", r.af,
	      af_long, r.ix);
	CHECK(z80_total(1) == total_long, "CPU 1 in slices: total %llu, want %llu", (unsigned long long)z80_total(1),
	      (unsigned long long)total_long);
	CHECK(fetches[1] == fetches_long, "CPU 1 in slices: %d opcode fetches, want %d", fetches[1], fetches_long);

	if (fails) {
		printf("lib/z80: %d failed\n", fails);
		return 1;
	}
	printf("lib/z80: ok\n");
	return 0;
}
