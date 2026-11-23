// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// sched.h -- MAME's device scheduler (src/emu/schedule.cpp and diexec.cpp,
// MAME 0.288) in C, for a board of N CPUs of any kind: timeslices to the
// next timer, the quantum and the perfect quantum (boost_interleave),
// timers, synchronize, abort_timeslice, suspend and resume, and input
// lines set through MAME's event queue. The CPUs run the cycles MAME's do,
// in the same order, so a board built on it can match MAME frame by frame.
// From the Star Trek and Time Pilot plugins (st.c, tp.c).
//
// ============================================================================
// THE CPU INTERFACE
//
// A CPU core plugs in with MAME's execute_run semantics. For a core named
// <cpu> (z80, mcs48, m6809, ...), with `cpu` an index (or a state pointer):
//
//   int  <cpu>_run(cpu, cycles)     run a timeslice of `cycles`; return the
//                                   cycles run: cycles - icount left - the
//                                   cycles an abort took back. An instruction
//                                   may run past the end (icount < 0); those
//                                   count as run.
//   void <cpu>_abort(cpu)           abort_timeslice: if icount > 0, take it
//                                   back (cycles_stolen += icount;
//                                   cycles_running -= icount; icount = 0).
//                                   Called from inside the run (a handler,
//                                   or the scheduler when a timer is set).
//   int  <cpu>_done(cpu)            cycles run so far in this timeslice:
//                                   cycles_running - icount
//   void <cpu>_adjust_icount(cpu, delta)   from a handler: icount += delta
//   uint64_t <cpu>_total(cpu)       total cycles since the CPU's reset
//   void <cpu>_set_irq(cpu, state), <cpu>_set_nmi(cpu, state)
//                                   execute_set_input: 1 ASSERT, 0 CLEAR
//   void <cpu>_reset(cpu)           device_reset
//
// The scheduler uses only run, abort, done (and reset, for a RESET line).
// The board names them in macros, defined before it includes sched.c (so
// the calls inline; a board with several kinds of CPU switches on d):
//
//   #define SCHED_CPUS 3                    the CPUs, d = 0 .. SCHED_CPUS-1,
//                                           in MAME's device order
//   #define SCHED_RUN(d, cycles)   ...      <cpu>_run for CPU d
//   #define SCHED_ABORT(d)         ...      <cpu>_abort
//   #define SCHED_DONE(d)          ...      <cpu>_done
//   #define SCHED_RESET(d)         ...      device_reset (optional: for a
//                                           RESET line let go; default none)
//
//   e.g. two Z80s and an 8035:
//   #define SCHED_RUN(d, n)  ((d) < 2 ? z80_run(d, n) : snd48_run(n))
//
// Optional sizes (defaults): SCHED_TIMERS 32 (the board's timers and the
// temporary ones outstanding at once), SCHED_LINES 8 (input lines),
// SCHED_QUANTA 8, SCHED_KEEPS 8 (sched_keep).
//
// The board's .c file:
//
//   #include "sched/sched.h"
//   #include "z80/z80.h"
//   #define SCHED_CPUS 2
//   #define SCHED_RUN(d, n)  z80_run(d, n)
//   #define SCHED_ABORT(d)   z80_abort(d)
//   #define SCHED_DONE(d)    z80_done(d)
//   #define SCHED_RESET(d)   z80_reset(d)
//   #include "sched/sched.c"
//
// Other files may include sched.h and call these functions (they're not
// static); only one file includes sched.c.
// ============================================================================
//
// Time is MAME's attotime as one 64-bit number: attoseconds from the epoch,
// a whole second (sched_epoch() seconds from power on). The epoch moves on
// a second at a time, between timeslices, and every time the scheduler
// holds moves with it; a time the board keeps (a VBLANK's start, say) moves
// too if the board hands it to sched_keep(). So a time is good for about 8
// seconds past the epoch: a delay must be shorter than that (or never).
#ifndef SCHED_H
#define SCHED_H

#include <stdbool.h>
#include <stdint.h>

typedef int64_t sched_time_t;                       // attoseconds from the epoch
#define SCHED_SEC 1000000000000000000LL             // ATTOSECONDS_PER_SECOND
#define SCHED_NEVER INT64_MAX                       // attotime::never
#define SCHED_HZ(hz) (SCHED_SEC / (hz))             // HZ_TO_ATTOSECONDS: a period
#define SCHED_MSEC(n) ((sched_time_t)(n) * 1000000000000000LL)
#define SCHED_USEC(n) ((sched_time_t)(n) * 1000000000000LL)
#define SCHED_NSEC(n) ((sched_time_t)(n) * 1000000000LL)

// line states (MAME's CLEAR_LINE, ASSERT_LINE, HOLD_LINE)
#define SCHED_CLEAR_LINE 0
#define SCHED_ASSERT_LINE 1
#define SCHED_HOLD_LINE 2

// suspend reasons (device_execute_interface's)
#define SCHED_SUSPEND_HALT 0x0001u          // the HALT line
#define SCHED_SUSPEND_RESET 0x0002u         // the RESET line (and before the machine's reset)
#define SCHED_SUSPEND_SPIN 0x0004u
#define SCHED_SUSPEND_TRIGGER 0x0008u
#define SCHED_SUSPEND_DISABLE 0x0010u
#define SCHED_SUSPEND_TIMESLICE 0x0020u     // yield: to the next timeslice
#define SCHED_SUSPEND_CLOCK 0x0040u
#define SCHED_SUSPEND_ANY 0xffffffffu

// ---- power on ----------------------------------------------------------------------------
// sched_init first, then each CPU in MAME's device order. Each CPU starts
// suspended (SCHED_SUSPEND_RESET), as MAME's do until the machine's reset:
// the board's reset calls sched_reset_cpu(d) with each core's reset.
void sched_init(void);
// clock: the device's clock in Hz; divider: its clocks a cycle (MAME's
// execute_clocks_to_cycles, rounded up: an 8035 15, a Z80 1); min_cycles:
// its execute_min_cycles (a Z80 1, an 8035 1), for the perfect interleave.
// Take both from the core's MAME header.
void sched_cpu(int d, uint32_t clock, uint32_t divider, int min_cycles);
void sched_max_quantum(sched_time_t q);   // config's set_maximum_quantum (default 1/60 s)
void sched_perfect_cpu(int d);            // config's set_perfect_quantum(device)
// interface_pre_reset and post_reset: CPU d's total cycles 0, every suspend
// reason cleared, its lines' vectors their defaults. With the core's reset.
void sched_reset_cpu(int d);

// ---- running -----------------------------------------------------------------------------
void sched_timeslice(void);               // device_scheduler::timeslice: to the next timer, then the timers due
void sched_run_to(uint64_t tick, uint32_t hz);   // timeslices while the base time is before tick / hz s from power on
sched_time_t sched_now(void);             // machine().time(): the timer's in a callback, the running CPU's, else the base
sched_time_t sched_base(void);            // the base time: where every CPU has got to
uint32_t sched_epoch(void);               // the whole seconds the times count from
uint64_t sched_ticks(sched_time_t t, uint32_t hz);   // attotime::as_ticks(hz) of t (from the epoch): from power on
void sched_keep(sched_time_t *t);         // a time the board keeps: moved with the epoch (SCHED_KEEPS of them)
int sched_executing(void);                // the CPU running now, -1 none
void sched_abort(void);                   // abort_timeslice: the running CPU's slice ends
sched_time_t sched_local_time(int d);     // the CPU's local time (with the cycles of its slice so far)
uint64_t sched_total_cycles(int d);       // total_cycles(): since its reset, eaten ones too
void sched_suspend(int d, uint32_t reason, bool eat);   // suspend: eat, the cycles count as run while it's out
void sched_resume(int d, uint32_t reason);
bool sched_suspended(int d, uint32_t reason);
void sched_yield(int d);                  // yield: out until the next timeslice
void sched_add_quantum(sched_time_t quantum, sched_time_t duration);   // add_quantum (boost_interleave)
void sched_perfect_quantum(sched_time_t duration);                      // perfect_quantum: add_quantum(0, duration)

// ---- timers ------------------------------------------------------------------------------
// A callback gets the timer's param; in it, sched_now() is the timer's
// expiry. Timers expiring at the same time fire in the order they were set.
typedef void (*sched_cb_t)(int param);
int sched_timer_alloc(sched_cb_t cb);     // timer_alloc: a timer of the board's (not set: never); its id
void sched_timer_adjust(int t, sched_time_t delay, int param, sched_time_t period);   // emu_timer::adjust (period SCHED_NEVER: once)
void sched_timer_adjust_at(int t, sched_time_t when, int param, sched_time_t period); // the same, to fire at `when` (from the epoch)
bool sched_timer_enable(int t, bool on);  // emu_timer::enable: the old state
bool sched_timer_enabled(int t);
int sched_timer_param(int t);
sched_time_t sched_timer_expire(int t);   // from the epoch (SCHED_NEVER: never)
sched_time_t sched_timer_remaining(int t);
sched_time_t sched_timer_elapsed(int t);
void sched_timer_set(sched_time_t delay, sched_cb_t cb, int param);   // timer_set: a one-shot of its own
void sched_synchronize(sched_cb_t cb, int param);                     // synchronize: timer_set(0)

// ---- input lines (device_execute_interface::set_input_line) ----------------------------
// The board numbers its lines 0 .. SCHED_LINES-1 and ties each to a CPU.
// sched_set_input queues the change, and a synchronize applies the queue
// in a timer callback, as MAME does: an ordinary line calls set(state)
// (the core's execute_set_input: 1 for ASSERT or HOLD, 0 for CLEAR). A
// RESET line suspends the CPU (eating its cycles) while held, and when let
// go calls sched_reset_cpu(d), SCHED_RESET(d) and resumes it. A HALT line
// suspends it while held.
enum { SCHED_LINE_NORMAL, SCHED_LINE_RESET, SCHED_LINE_HALT };
void sched_line(int line, int d, int kind, int vector, void (*set)(int state));   // vector: the core's default_irq_vector (Z80 0xff)
void sched_set_input(int line, int state);                       // set_input_line
void sched_set_input_and_vector(int line, int state, int vector);
void sched_set_input_vector(int line, int vector);                // set_input_line_vector (the stored one)
int sched_input_state(int line);                                  // the state applied last (m_curstate)
// standard_irq_callback's part: a HOLD_LINE let go (set(0)), the vector
// returned. Call it from the core's interrupt acknowledge.
int sched_irq_ack(int line);

#endif
