// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
//
// z80.c -- see z80.h. MAME 0.288's z80_device (z80.cpp, z80.h and z80.lst)
// in C: its registers, the helpers z80.lst calls, device_start,
// device_reset, execute_set_input and execute_run. The instructions are
// z80_ops.h, made from z80.lst by z80gen.py. Left out: the variants
// (NSC800, R800, Z80N, T6A84), the WAIT line, the refresh and no-MREQ
// callbacks, and the daisy chain (a board can make one with Z80_IRQ_VECTOR
// and Z80_RETI). From the Time Pilot and Star Trek plugins (Parrot Cart's
// timeplt and startrek), which had it from Donkey Kong's.
//
// A template: the board's .c file defines Z80_CPUS and its connections and
// includes this (z80.h, README.md). Its Z80s share this code (100 KB on
// the cart: its RAM holds one; Z80_COMPACT makes it smaller). The one
// running has its registers in Z; each of the others has them in z_saved,
// and z80_run swaps them as a slice starts (a straight copy of words,
// cheaper than reaching the registers through a pointer, which costs every
// instruction).
// (Built alone, without Z80_CPUS, this file is empty.)
#ifdef Z80_CPUS
#include "z80.h"

#include <string.h>

// ---- the board's connections, and their defaults -----------------------------------------
// handlers (the cycle count synced: they may call z80_done, z80_abort,
// z80_adjust_icount, z80_total)
#ifndef Z80_RD
#define Z80_RD(cpu, a) ((void)(cpu), (void)(a), 0)              // a read from a NULL page (MAME's unmapped: 0)
#endif
#ifndef Z80_WR
#define Z80_WR(cpu, a, d) ((void)(cpu), (void)(a), (void)(d))   // a write to a NULL page
#endif
#ifndef Z80_RD_OP
#define Z80_RD_OP(cpu, a) Z80_RD(cpu, a)                         // an opcode fetch from a NULL page of z80_op_page
#endif
#ifndef Z80_IN
#define Z80_IN(cpu, port) ((void)(cpu), (void)(port), 0)        // IN (the port is 16 bits, as the Z80 puts it out)
#endif
#ifndef Z80_OUT
#define Z80_OUT(cpu, port, d) ((void)(cpu), (void)(port), (void)(d))
#endif
#ifndef Z80_IRQ_VECTOR
#define Z80_IRQ_VECTOR(cpu) ((void)(cpu), 0xff)                 // the acknowledge's vector (standard_irq_callback)
#endif
// optional handlers: Z80_IRQACK(cpu) (irqack_cb(1), before the vector),
// Z80_RETI(cpu) (RETI's daisy_call_reti_device), Z80_BUSACK(cpu, state)
// (busack_cb), Z80_TRACE(cpu) (before each instruction:
// debugger_instruction_hook; z80_regs and z80_total are as MAME's trace
// shows them). Z80_HALT(cpu, state) (halt_cb) isn't synced.
//
// inline, not synced
#ifndef Z80_M1_CYCLES
#define Z80_M1_CYCLES 4                     // z80_set_m1_cycles (an M1's cycles, of which 2 refresh)
#endif
#ifndef Z80_MREQ_CYCLES
#define Z80_MREQ_CYCLES 3                   // z80_set_mreq_cycles
#endif
#ifndef Z80_IORQ_CYCLES
#define Z80_IORQ_CYCLES 4                   // z80_set_iorq_cycles
#endif
// a board's wait states, as MAME's taps that adjust_icount(-n) at each
// access (after it): the cycles each access to a takes past the standard
#ifndef Z80_M1_WAIT
#define Z80_M1_WAIT(cpu, a) 0               // an opcode fetch (M1)
#endif
#ifndef Z80_RD_WAIT
#define Z80_RD_WAIT(cpu, a) 0               // a memory read (an argument, data, the stack)
#endif
#ifndef Z80_WR_WAIT
#define Z80_WR_WAIT(cpu, a) 0
#endif
#ifndef Z80_IN_WAIT
#define Z80_IN_WAIT(cpu, port) 0
#endif
#ifndef Z80_OUT_WAIT
#define Z80_OUT_WAIT(cpu, port) 0
#endif
#ifndef Z80_OPCODE
#define Z80_OPCODE(cpu, a, op) ((void)0)    // after each opcode fetch (M1), with the byte
#endif
#ifndef Z80_WR_PAGED
#define Z80_WR_PAGED(cpu, a) 1              // 0: this write goes to Z80_WR though its page is set
#endif

// (the running CPU's registers: a name kept to this file, #undef at its end)
#define Z z80i_Z

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define Z80_HOT __attribute__((section(".time_critical.z80")))
#else
#define Z80_HOT
#endif
#define Z80_INLINE static inline __attribute__((always_inline))
// Z80_COMPACT: a smaller core, for a plugin short of RAM, at some cost in
// speed (README.md, "Cost on the cart"). 1 (-DZ80_COMPACT): the flag
// helpers (the ALU, shifts, BIT, INC and DEC) are called instead of
// inlined at every instruction. 2: the memory and I/O accessors too. The
// core does the same either way, cycle for cycle.
#ifndef Z80_COMPACT
#define Z80_COMPACT 0
#endif
#if Z80_COMPACT >= 1
#define Z80_ALU static __attribute__((noinline)) Z80_HOT
#else
#define Z80_ALU Z80_INLINE
#endif
#if Z80_COMPACT >= 2
#define Z80_MEM static __attribute__((noinline)) Z80_HOT
#else
#define Z80_MEM Z80_INLINE
#endif

typedef union { struct { u8 l, h; } b; u16 w; } pair16_t;

typedef struct {
	pair16_t prvpc, pc, sp, af, bc, de, hl, ix, iy, wz, af2, bc2, de2, hl2;
	u8 r, r2, halt, im, i, nmi_state, irq_state, busack_state, service_attention;
	bool iff1, iff2;
	int busreq_state;
	u16 ea;
	struct {
		u8 s_val, z_val, yx_val, h_val, pv_val;
		bool n, c;
		u8 q, qtemp;
	} f;
	int icount, tmp_irq_vector;
	pair16_t shared_data, shared_data2;
	u32 ref;
	int cycles_running, cycles_stolen;
} zstate_t;

const u8 *z80_rd_page[Z80_CPUS][256];
u8 *z80_wr_page[Z80_CPUS][256];
#ifdef Z80_OPCODES
const u8 *z80_op_page[Z80_CPUS][256];
#endif

static zstate_t Z;                      // the running CPU's (z_live's)
static uint64_t z_total[Z80_CPUS];      // each CPU's cycles since its reset, before this slice
static int z_exec = -1;                 // the CPU in z80_run, -1 none
#if Z80_CPUS > 1
static zstate_t z_saved[Z80_CPUS];      // each other CPU's
static int z_live;
#define ZCPU z_live
// a CPU's registers wherever they are (for the calls from the board: a
// CPU's line set while another runs, say)
static inline zstate_t *zs(int cpu) { return cpu == z_live ? &Z : &z_saved[cpu]; }

typedef uint32_t __attribute__((may_alias)) zword_t;
#define ZW (sizeof(zstate_t) / sizeof(zword_t))
_Static_assert(sizeof(zstate_t) % sizeof(zword_t) == 0 && ZW <= 24, "zstate_t: whole words, 24 at most");
static inline void zcopy(zstate_t *d, const zstate_t *s) {
	zword_t *dw = (zword_t *)(void *)d;
	const zword_t *sw = (const zword_t *)(const void *)s;
#define ZC(i) if ((i) < ZW) dw[i] = sw[i];
	ZC(0) ZC(1) ZC(2) ZC(3) ZC(4) ZC(5) ZC(6) ZC(7) ZC(8) ZC(9) ZC(10) ZC(11)
	ZC(12) ZC(13) ZC(14) ZC(15) ZC(16) ZC(17) ZC(18) ZC(19) ZC(20) ZC(21) ZC(22) ZC(23)
#undef ZC
}

// the CPU's registers into Z, as z80_run starts a slice (z80_run doesn't
// nest: a handler must not run another Z80)
static inline void z_select(int cpu) {
	if (cpu == z_live) return;
	zcopy(&z_saved[z_live], &Z);
	zcopy(&Z, &z_saved[cpu]);
	z_live = cpu;
}
#else
#define ZCPU 0
static inline zstate_t *zs(int cpu) { (void)cpu; return &Z; }
static inline void z_select(int cpu) { (void)cpu; }
#endif

// z80.inc
#define HAS_LDAIR_QUIRK 0
#define CF  0x01
#define NF  0x02
#define PF  0x04
#define VF  PF
#define HF  0x10
#define YXF 0x28
#define ZF  0x40
#define SF  0x80
#define PRVPC  Z.prvpc.w
#define PC     Z.pc.w
#define SP     Z.sp.w
#define Q      Z.f.q
#define QT     Z.f.qtemp
#define I      Z.i
#define R      Z.r
#define R2     Z.r2
#define AF     Z.af.w
#define A      Z.af.b.h
#define F      Z.af.b.l
#define BC     Z.bc.w
#define B      Z.bc.b.h
#define C      Z.bc.b.l
#define DE     Z.de.w
#define D      Z.de.b.h
#define E      Z.de.b.l
#define HL     Z.hl.w
#define H      Z.hl.b.h
#define L      Z.hl.b.l
#define IX     Z.ix.w
#define HX     Z.ix.b.h
#define LX     Z.ix.b.l
#define IY     Z.iy.w
#define HY     Z.iy.b.h
#define LY     Z.iy.b.l
#define WZ     Z.wz.w
#define WZ_H   Z.wz.b.h
#define WZ_L   Z.wz.b.l
#define TDAT   Z.shared_data.w
#define TDAT2  Z.shared_data2.w
#define TDAT_H Z.shared_data.b.h
#define TDAT_L Z.shared_data.b.l
#define TDAT8  Z.shared_data.b.l

// z80.h: the service attention bits and the flags' accessors
#define SA_BUSREQ      0
#define SA_NMI_PENDING 1
#define SA_IRQ_ON      2
#define SA_HALT        3
#define SA_AFTER_EI    4
#define SA_AFTER_LDAIR 5
#define SET_SA(bit, state) ((state) ? (Z.service_attention |= (u8)(1u << (bit))) : (Z.service_attention &= (u8)~(1u << (bit))))
#define GET_SA(bit) ((Z.service_attention >> (bit)) & 1)
#define F_S()  (Z.f.s_val & 0x80)
#define F_Z()  (Z.f.z_val ? 0 : 0x40)
#define F_YX() (Z.f.yx_val & 0x28)
#define F_H()  (Z.f.h_val & 0x10)
#define F_PV() z80i_flag_pv(Z.f.pv_val)
#define Z80_CLEAR 0                     // CLEAR_LINE

// the bus timing z80_ops.h takes (z80_device's m_m1_cycles, m_mreq_cycles, m_iorq_cycles)
#define Z80_M1   (Z80_M1_CYCLES)
#define Z80_MREQ (Z80_MREQ_CYCLES)
#define Z80_IORQ (Z80_IORQ_CYCLES)

// no refresh or no-MREQ callback, so nothing is called
#define refresh_en 0
#define nomreq_en 0
#define NOCB(...) ((void)0)

#define z80i_swap(a, b) do { __typeof__(a) t_ = (a); (a) = (b); (b) = t_; } while (0)
#define SWAP16(a, b) z80i_swap(a, b)

Z80_INLINE u8 z80i_flag_pv(u8 val) {
	val ^= val >> 4;
	val ^= val << 2;
	val ^= val >> 1;
	return ~val & 0x04;
}

// ---- memory and I/O ------------------------------------------------------------------------
// (ic, the cycles left, is the execute function's own: a handler sees it
// in Z.icount, and a change it makes there is taken back)
#define SYNC_CALL(x) do { Z.icount = *ic; x; *ic = Z.icount; } while (0)

Z80_MEM u8 z80i_rd(int *ic, const u8 *const *rdp, int cpu, u16 a) {
	(void)cpu;
	u8 v;
	const u8 *p = rdp[a >> 8];
	if (__builtin_expect(p != 0, 1)) v = p[a & 0xff];
	else SYNC_CALL(v = (u8)(Z80_RD(cpu, a)));
	*ic -= Z80_RD_WAIT(cpu, a);
	return v;
}
// an M1 fetch: the opcode space's page (or handler), its wait, the board's look at it
Z80_MEM u8 z80i_rd_op(int *ic, const u8 *const *opp, int cpu, u16 a) {
	(void)cpu;
	u8 v;
	const u8 *p = opp[a >> 8];
#ifdef Z80_OPCODES
	if (__builtin_expect(p != 0, 1)) v = p[a & 0xff];
	else SYNC_CALL(v = (u8)(Z80_RD_OP(cpu, a)));
#else
	if (__builtin_expect(p != 0, 1)) v = p[a & 0xff];
	else SYNC_CALL(v = (u8)(Z80_RD(cpu, a)));
#endif
	*ic -= Z80_M1_WAIT(cpu, a);
	Z80_OPCODE(cpu, a, v);
	return v;
}
Z80_MEM void z80i_wr(int *ic, u8 *const *wrp, int cpu, u16 a, u8 v) {
	(void)cpu;
	u8 *p = wrp[a >> 8];
	if (__builtin_expect(p != 0 && (Z80_WR_PAGED(cpu, a)), 1)) p[a & 0xff] = v;
	else SYNC_CALL(Z80_WR(cpu, a, v));
	*ic -= Z80_WR_WAIT(cpu, a);
}
Z80_MEM u8 z80i_io_in(int *ic, int cpu, u16 port) {
	(void)cpu;
	u8 v;
	SYNC_CALL(v = (u8)(Z80_IN(cpu, port)));
	*ic -= Z80_IN_WAIT(cpu, port);
	return v;
}
Z80_MEM void z80i_io_out(int *ic, int cpu, u16 port, u8 v) {
	(void)cpu;
	SYNC_CALL(Z80_OUT(cpu, port, v));
	*ic -= Z80_OUT_WAIT(cpu, port);
}
Z80_MEM int z80i_irq_vector(int *ic, int cpu) {
	(void)cpu;
	int v;
	SYNC_CALL(v = (int)(Z80_IRQ_VECTOR(cpu)));
	return v;
}
#undef SYNC_CALL

// the names z80_ops.h uses (inside execute: ic, rdp, wrp, opp, zc)
#define RD(a) z80i_rd(&ic, rdp, zc, (u16)(a))
#define WR(a, v) z80i_wr(&ic, wrp, zc, (u16)(a), (u8)(v))
#define RD_OP_AT(a) z80i_rd_op(&ic, opp, zc, (u16)(a))
#define RD_OP() RD_OP_AT(PC)
#define RD_ARG() RD(PC)
#define IO_IN(p) z80i_io_in(&ic, zc, (u16)(p))
#define IO_OUT(p, v) z80i_io_out(&ic, zc, (u16)(p), (u8)(v))
#define IRQ_VECTOR() z80i_irq_vector(&ic, zc)
#define Z80_SYNCED(x) do { Z.icount = ic; x; ic = Z.icount; } while (0)
#ifdef Z80_IRQACK
#define IRQACK() Z80_SYNCED(Z80_IRQACK(zc))
#else
#define IRQACK() do { } while (0)
#endif
#ifdef Z80_RETI
#define RETI_HOOK() Z80_SYNCED(Z80_RETI(zc))
#else
#define RETI_HOOK() do { } while (0)
#endif
#ifdef Z80_BUSACK
#define BUSACK_CB(state) Z80_SYNCED(Z80_BUSACK(zc, state))
#else
#define BUSACK_CB(state) do { } while (0)
#endif
#ifdef Z80_TRACE
#define Z80_HOOK() Z80_SYNCED(Z80_TRACE(zc))
#else
#define Z80_HOOK() do { } while (0)
#endif
#ifdef Z80_HALT
#define HALT_CB(state) Z80_HALT(ZCPU, state)
#define HALT_CB_OF(cpu, state) Z80_HALT(cpu, state)
#else
#define HALT_CB(state) do { } while (0)
#define HALT_CB_OF(cpu, state) do { } while (0)
#endif

// ---- z80.cpp's helpers ---------------------------------------------------------------------
static u8 z80i_get_f_of(const zstate_t *z) {
	u8 f = 0;
	f |= z->f.s_val & 0x80;
	f |= z->f.z_val ? 0 : 0x40;
	f |= z->f.yx_val & 0x28;
	f |= z->f.h_val & 0x10;
	f |= z80i_flag_pv(z->f.pv_val);
	f |= z->f.n ? NF : 0;
	f |= z->f.c ? CF : 0;
	return f;
}
#define z80i_get_f() z80i_get_f_of(&Z)

Z80_INLINE void z80i_set_f_of(zstate_t *z, u8 f) {
	z->f.s_val = f;
	z->f.z_val = !(f & ZF);
	z->f.yx_val = f;
	z->f.h_val = f;
	z->f.pv_val = !(f & PF);
	z->f.n = f & NF;
	z->f.c = f & CF;
}
static void z80i_set_f(u8 f) { z80i_set_f_of(&Z, f); }

Z80_INLINE void z80i_halt(void) {
	if (!Z.halt) {
		Z.halt = 1;
		SET_SA(SA_HALT, 1);
		HALT_CB(1);
	}
}

Z80_INLINE void z80i_leave_halt(void) {
	if (Z.halt) {
		Z.halt = 0;
		SET_SA(SA_HALT, 0);
		HALT_CB(0);
	}
}

Z80_ALU void z80i_inc(u8 *r) {
	++*r;
	QT = 0;
	// keep C
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = *r;
	Z.f.pv_val = *r != 0x80;
	Z.f.h_val = (*r & 0x0f) == 0x00 ? HF : 0;
	Z.f.n = 0;
}

Z80_ALU void z80i_dec(u8 *r) {
	--*r;
	QT = 0;
	// keep C
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = *r;
	Z.f.pv_val = *r != 0x7f;
	Z.f.h_val = (*r & 0x0f) == 0x0f ? HF : 0;
	Z.f.n = 1;
}

Z80_ALU void z80i_rlca(void) {
	A = (A << 1) | (A >> 7);
	QT = 0;
	// keep SZP
	Z.f.yx_val = A;
	Z.f.h_val = Z.f.n = 0;
	Z.f.c = A & 0x01;
}

Z80_ALU void z80i_rrca(void) {
	const u8 a0 = A;
	A = (a0 >> 1) | (a0 << 7);
	QT = 0;
	// keep SZP
	Z.f.yx_val = A;
	Z.f.h_val = Z.f.n = 0;
	Z.f.c = a0 & 0x01;
}

Z80_ALU void z80i_rla(void) {
	u8 res = (A << 1) + Z.f.c;
	QT = 0;
	// keep SZP
	Z.f.yx_val = res;
	Z.f.h_val = Z.f.n = 0;
	Z.f.c = A & 0x80;
	A = res;
}

Z80_ALU void z80i_rra(void) {
	u8 res = (Z.f.c << 7) | (A >> 1);
	QT = 0;
	// keep SZP
	Z.f.yx_val = res;
	Z.f.h_val = Z.f.n = 0;
	Z.f.c = A & 0x01;
	A = res;
}

Z80_ALU void z80i_add_a(u8 value) {
	const u16 res = A + value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = (u8)res;
	Z.f.c = res & 0x100;
	Z.f.h_val = (A & 0x0f) + (value & 0x0f);
	Z.f.pv_val = !((A ^ res) & (value ^ res) & 0x80);
	Z.f.n = 0;
	A = (u8)res;
}

Z80_ALU void z80i_adc_a(u8 value) {
	const int c = Z.f.c;
	const u16 res = A + value + c;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = (u8)res;
	Z.f.c = res & 0x100;
	Z.f.h_val = (A & 0x0f) + (value & 0x0f) + c;
	Z.f.pv_val = !((A ^ res) & (value ^ res) & 0x80);
	Z.f.n = 0;
	A = (u8)res;
}

Z80_ALU void z80i_sub_a(u8 value) {
	const u16 res = A - value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = (u8)res;
	Z.f.c = res & 0x100;
	Z.f.h_val = (A & 0x0f) - (value & 0x0f);
	Z.f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
	Z.f.n = 1;
	A = (u8)res;
}

Z80_ALU void z80i_sbc_a(u8 value) {
	const int c = Z.f.c;
	const u16 res = A - value - c;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = (u8)res;
	Z.f.c = res & 0x100;
	Z.f.h_val = (A & 0x0f) - (value & 0x0f) - c;
	Z.f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
	Z.f.n = 1;
	A = (u8)res;
}

Z80_ALU void z80i_neg(void) {
	u8 value = A;
	A = 0;
	z80i_sub_a(value);
}

Z80_ALU void z80i_daa(void) {
	u8 a = A;
	if (Z.f.n) {
		if (F_H() || ((A & 0xf) > 9)) a -= 6;
		if (Z.f.c || (A > 0x99)) a -= 0x60;
	} else {
		if (F_H() || ((A & 0xf) > 9)) a += 6;
		if (Z.f.c || (A > 0x99)) a += 0x60;
	}
	QT = 0;
	// keep N
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = Z.f.yx_val = a;
	Z.f.h_val = A ^ a;
	Z.f.c = Z.f.c || A > 0x99;
	A = a;
}

Z80_ALU void z80i_and_a(u8 value) {
	A &= value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = Z.f.yx_val = A;
	Z.f.n = Z.f.c = 0;
	Z.f.h_val = HF;
}

Z80_ALU void z80i_or_a(u8 value) {
	A |= value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = Z.f.yx_val = A;
	Z.f.h_val = Z.f.n = Z.f.c = 0;
}

Z80_ALU void z80i_xor_a(u8 value) {
	A ^= value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = Z.f.yx_val = A;
	Z.f.h_val = Z.f.n = Z.f.c = 0;
}

Z80_ALU void z80i_cp(u8 value) {
	const u16 res = A - value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = (u8)res;
	Z.f.yx_val = value;
	Z.f.c = res & 0x100;
	Z.f.h_val = (A & 0x0f) - (value & 0x0f);
	Z.f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
	Z.f.n = 1;
}

Z80_INLINE void z80i_exx(void) {
	z80i_swap(Z.bc, Z.bc2);
	z80i_swap(Z.de, Z.de2);
	z80i_swap(Z.hl, Z.hl2);
}

#define SHIFT_FLAGS(res, carry)                                                  \
	do {                                                                         \
		QT = 0;                                                                  \
		Z.f.s_val = Z.f.z_val = Z.f.pv_val = Z.f.yx_val = (res);                 \
		Z.f.h_val = Z.f.n = 0;                                                   \
		Z.f.c = (carry);                                                         \
	} while (0)
Z80_ALU u8 z80i_rlc(u8 value) { const u8 res = ((value << 1) | (value >> 7)) & 0xff; SHIFT_FLAGS(res, value & 0x80); return res; }
Z80_ALU u8 z80i_rrc(u8 value) { const u8 res = ((value >> 1) | (value << 7)) & 0xff; SHIFT_FLAGS(res, value & 0x01); return res; }
Z80_ALU u8 z80i_rl(u8 value) { const u8 res = ((value << 1) + Z.f.c) & 0xff; SHIFT_FLAGS(res, value & 0x80); return res; }
Z80_ALU u8 z80i_rr(u8 value) { const u8 res = ((value >> 1) | (Z.f.c << 7)) & 0xff; SHIFT_FLAGS(res, value & 0x01); return res; }
Z80_ALU u8 z80i_sla(u8 value) { const u8 res = (value << 1) & 0xff; SHIFT_FLAGS(res, value & 0x80); return res; }
Z80_ALU u8 z80i_sra(u8 value) { const u8 res = ((value >> 1) | (value & 0x80)) & 0xff; SHIFT_FLAGS(res, value & 0x01); return res; }
Z80_ALU u8 z80i_sll(u8 value) { const u8 res = ((value << 1) | 0x01) & 0xff; SHIFT_FLAGS(res, value & 0x80); return res; }
Z80_ALU u8 z80i_srl(u8 value) { const u8 res = (value >> 1) & 0xff; SHIFT_FLAGS(res, value & 0x01); return res; }

Z80_ALU void z80i_bit(int b, u8 value) {
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = value & (1 << b);
	Z.f.h_val = HF;
	Z.f.n = 0;
	Z.f.yx_val = value;
}

Z80_ALU void z80i_bit_hl(int b, u8 value) {
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = value & (1 << b);
	Z.f.h_val = HF;
	Z.f.n = 0;
	Z.f.yx_val = WZ_H;
}

Z80_ALU void z80i_bit_xy(int b, u8 value) {
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = value & (1 << b);
	Z.f.h_val = HF;
	Z.f.n = 0;
	Z.f.yx_val = Z.ea >> 8;
}

Z80_INLINE u8 z80i_res(int b, u8 value) { return value & ~(1 << b); }
Z80_INLINE u8 z80i_set(int b, u8 value) { return value | (1 << b); }

Z80_ALU void z80i_block_io_interrupted_flags(void) {
	Z.f.yx_val = PC >> 8;
	const u8 pv_old = F_PV();
	if (Z.f.c) {
		Z.f.h_val = 0;
		if (TDAT8 & 0x80) {
			Z.f.pv_val = (B - 1) & 0x07;
			if ((B & 0x0f) == 0x00) Z.f.h_val = HF;
		} else {
			Z.f.pv_val = (B + 1) & 0x07;
			if ((B & 0x0f) == 0x0f) Z.f.h_val = HF;
		}
	} else {
		Z.f.pv_val = B & 0x07;
	}
	Z.f.pv_val = (pv_old ^ F_PV()) & PF;
}

Z80_INLINE void z80i_ei(void) {
	Z.iff1 = Z.iff2 = 1;
	SET_SA(SA_AFTER_EI, 1);
}

Z80_INLINE void z80i_illegal_1(void) {}
Z80_INLINE void z80i_illegal_2(void) {}

// ---- device_start, device_reset, execute_set_input -----------------------------------------
// (all on the CPU's registers where they are, through zs: a handler of one
// Z80 may reset another while it runs)
#define ZSA(z, bit, state) ((state) ? ((z)->service_attention |= (u8)(1u << (bit))) : ((z)->service_attention &= (u8)~(1u << (bit))))

void z80_start(int cpu) {
	zstate_t *z = zs(cpu);
	memset(z, 0, sizeof *z);    // PC, SP, AF, BC .. HL', WZ, R, I, IM, IFF1/2, the lines: 0
	z80i_set_f_of(z, 0);
	z->ix.w = z->iy.w = 0xffff; // IX and IY are FFFF after a reset!
	z->f.z_val = 0;             // Zero flag is set
	z->ref = 0;                 // (the instruction boundary: z80gen.py)
	z_total[cpu] = 0;
}

void z80_reset(int cpu) {
	zstate_t *z = zs(cpu);
	z_total[cpu] = 0;           // (device_execute_interface::interface_pre_reset)
	if (z->halt) {              // leave_halt
		z->halt = 0;
		ZSA(z, SA_HALT, 0);
		HALT_CB_OF(cpu, 0);
	}
	z->ref = 0;                 // (the instruction boundary: z80gen.py)
	z->pc.w = 0;
	z->wz.w = z->pc.w;
	z->i = 0;
	z->r = 0;
	z->r2 = 0;
	z->iff1 = 0;
	z->iff2 = 0;
	ZSA(z, SA_NMI_PENDING, 0);
	ZSA(z, SA_AFTER_EI, 0);
	ZSA(z, SA_AFTER_LDAIR, 0);
}

void z80_set_irq(int cpu, int state) {
	zstate_t *z = zs(cpu);
	z->irq_state = (u8)state;
	if (z->irq_state != Z80_CLEAR) ZSA(z, SA_IRQ_ON, 1);
	else ZSA(z, SA_IRQ_ON, 0);
}

void z80_set_nmi(int cpu, int state) {
	zstate_t *z = zs(cpu);
	// mark an NMI pending on the rising edge (not at the same time RESET is cleared)
	if (z->nmi_state == Z80_CLEAR && state != Z80_CLEAR && z80_total(cpu))
		ZSA(z, SA_NMI_PENDING, 1);
	z->nmi_state = (u8)state;
}

void z80_set_busreq(int cpu, int state) {
	zstate_t *z = zs(cpu);
	if (z->busreq_state == Z80_CLEAR && state != Z80_CLEAR)
		ZSA(z, SA_BUSREQ, 1);
	z->busreq_state = state;
}

// ---- execute_run ---------------------------------------------------------------------------
static Z80_HOT __attribute__((noinline)) void z80i_execute(void) {
	int ic = Z.icount;
	const int zc = ZCPU;
	(void)zc;
	const u8 *const *const rdp = z80_rd_page[zc];       // (in registers: no reload after a write)
	u8 *const *const wrp = z80_wr_page[zc];
#ifdef Z80_OPCODES
	const u8 *const *const opp = z80_op_page[zc];
#else
	const u8 *const *const opp = rdp;
#endif
#include "z80_ops.h"
suspend:
	Z.icount = ic;
}

int z80_run(int cpu, int cycles) {
	z_select(cpu);
	Z.cycles_running = cycles;
	Z.cycles_stolen = 0;
	Z.icount = cycles;
	z_exec = cpu;
	z80i_execute();
	z_exec = -1;
	const int ran = cycles - Z.icount - Z.cycles_stolen;
	z_total[cpu] += (uint64_t)ran;
	return ran;
}

void z80_abort(int cpu) {
	zstate_t *z = zs(cpu);
	if (z->icount > 0) {
		int delta = z->icount;
		z->cycles_stolen += delta;
		z->cycles_running -= delta;
		z->icount = 0;
	}
}

void z80_adjust_icount(int cpu, int delta) { zs(cpu)->icount += delta; }

void z80_eat_cycles(int cpu, int cycles) {
	zstate_t *z = zs(cpu);
	if (z_exec != cpu) return;
	if (cycles > z->icount) cycles = z->icount;
	z->icount -= cycles;
}

int z80_done(int cpu) { const zstate_t *z = zs(cpu); return z->cycles_running - z->icount; }

int z80_icount(int cpu) { return zs(cpu)->icount; }

uint64_t z80_total(int cpu) { return z_total[cpu] + (z_exec == cpu ? (uint64_t)z80_done(cpu) : 0); }

bool z80_busack(int cpu) { return zs(cpu)->busack_state != 0; }

bool z80_halted(int cpu) { return zs(cpu)->halt != 0; }

void z80_regs(int cpu, z80_regs_t *r) {
	const zstate_t *z = zs(cpu);
	r->pc = z->pc.w; r->sp = z->sp.w;
	r->af = (u16)(z->af.b.h << 8 | z80i_get_f_of(z));
	r->bc = z->bc.w; r->de = z->de.w; r->hl = z->hl.w; r->ix = z->ix.w; r->iy = z->iy.w;
	r->af2 = z->af2.w; r->bc2 = z->bc2.w; r->de2 = z->de2.w; r->hl2 = z->hl2.w;
	r->wz = z->wz.w;
	r->r = (u8)((z->r & 0x7f) | (z->r2 & 0x80));
	r->i = z->i; r->im = z->im; r->iff1 = z->iff1; r->iff2 = z->iff2; r->halt = z->halt;
}

void z80_set_regs(int cpu, const z80_regs_t *r) {
	zstate_t *z = zs(cpu);
	z->pc.w = r->pc; z->sp.w = r->sp;
	z->af.b.h = (u8)(r->af >> 8);
	z80i_set_f_of(z, (u8)r->af);
	z->bc.w = r->bc; z->de.w = r->de; z->hl.w = r->hl; z->ix.w = r->ix; z->iy.w = r->iy;
	z->af2.w = r->af2; z->bc2.w = r->bc2; z->de2.w = r->de2; z->hl2.w = r->hl2;
	z->wz.w = r->wz;
	z->r = r->r; z->r2 = r->r;
	z->i = r->i; z->im = r->im; z->iff1 = r->iff1 != 0; z->iff2 = r->iff2 != 0;
}

// (none of this file's names past it, but the API)
#undef Z
#undef Z80_HOT
#undef Z80_INLINE
#undef Z80_ALU
#undef Z80_MEM
#undef ZCPU
#undef ZW
#undef HAS_LDAIR_QUIRK
#undef CF
#undef NF
#undef PF
#undef VF
#undef HF
#undef YXF
#undef ZF
#undef SF
#undef PRVPC
#undef PC
#undef SP
#undef Q
#undef QT
#undef I
#undef R
#undef R2
#undef AF
#undef A
#undef F
#undef BC
#undef B
#undef C
#undef DE
#undef D
#undef E
#undef HL
#undef H
#undef L
#undef IX
#undef HX
#undef LX
#undef IY
#undef HY
#undef LY
#undef WZ
#undef WZ_H
#undef WZ_L
#undef TDAT
#undef TDAT2
#undef TDAT_H
#undef TDAT_L
#undef TDAT8
#undef SA_BUSREQ
#undef SA_NMI_PENDING
#undef SA_IRQ_ON
#undef SA_HALT
#undef SA_AFTER_EI
#undef SA_AFTER_LDAIR
#undef SET_SA
#undef GET_SA
#undef F_S
#undef F_Z
#undef F_YX
#undef F_H
#undef F_PV
#undef Z80_CLEAR
#undef Z80_M1
#undef Z80_MREQ
#undef Z80_IORQ
#undef refresh_en
#undef nomreq_en
#undef NOCB
#undef z80i_swap
#undef SWAP16
#undef z80i_get_f
#undef SHIFT_FLAGS
#undef ZSA
#undef RD
#undef WR
#undef RD_OP_AT
#undef RD_OP
#undef RD_ARG
#undef IO_IN
#undef IO_OUT
#undef IRQ_VECTOR
#undef Z80_SYNCED
#undef IRQACK
#undef RETI_HOOK
#undef BUSACK_CB
#undef Z80_HOOK
#undef HALT_CB
#undef HALT_CB_OF

#endif  // Z80_CPUS
