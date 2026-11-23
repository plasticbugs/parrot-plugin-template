// license:BSD-3-Clause
// copyright-holders:Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud
//
// mcs48.h -- the Intel MCS-48 (8035, 8039, 8048 family) as MAME 0.288's
// mcs48_cpu_device (src/devices/cpu/mcs48/mcs48.cpp) runs it: the same
// instructions, cycles, timer/counter, interrupts, ports, BUS, MOVX and
// expander (8243) operations, and the same timeslice behaviour, so a board
// built with it can be checked against MAME frame by frame. README.md says
// where it came from and how it was proven.
//
// The CPU's state is an mcs48_t, so a board can have several. The
// instructions are a template, mcs48.c: a plugin's .c file defines the
// CPU's connections and includes it (README.md has the list):
//
//   #define MCS48_ROM           snd_rom          // program memory (a pointer)
//   #define MCS48_P1_W(d)       dac = (d)        // port 1's latch, written
//   #define MCS48_T1()          (latch & 0x10)   // the T1 pin
//   ...
//   #include "mcs48/mcs48.c"                     // mcs48_run(), mcs48_reset(), mcs48_burn()
//
// For two CPUs wired differently, include it twice (from one file or two),
// each after its own connections and a name:
//   #define MCS48_NAME(x) usb48_##x   ->  usb48_run(), usb48_reset() ...
//
// What a scheduler calls, with MAME's execute_run semantics:
//
//   <name>_run(m, cycles)    run a timeslice: instructions while cycles are
//                            left (the last may overrun); returns the cycles
//                            run (more than asked if it overran, fewer if
//                            aborted). Counts them into m->total.
//   mcs48_abort(m)           end the running timeslice (abort_timeslice)
//   mcs48_adjust_icount(m,d) add d to the cycles left (negative: wait states)
//   mcs48_done(m)            cycles run so far in the running timeslice
//   mcs48_total(m)           cycles since the reset, this slice's so far too
//   mcs48_set_irq(m, state)  the INT pin: 1 asserted, 0 clear
//                            (MCS48_INPUT_IRQ; MAME applies it between slices)
//   <name>_reset(m)          device_reset: the total to 0 (as MAME's reset
//                            does), then the CPU's reset, which writes the ports
//   mcs48_init(m)            power on (device_start): all zero; then reset
//
// The CPU is named by a pointer to its state: in a callback, the macros
// see it as `m`, and m->user is the plugin's own.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MCS48_INPUT_IRQ 0

// timecount_enabled
#define MCS48_TIMER_ENABLED   0x01
#define MCS48_COUNTER_ENABLED 0x02

// the PSW's bits (bit 3 reads as 1; bits 2-0 are the stack pointer)
#define MCS48_C_FLAG 0x80
#define MCS48_A_FLAG 0x40
#define MCS48_F_FLAG 0x20
#define MCS48_B_FLAG 0x10

typedef struct mcs48 {
    // the registers, as MAME names them (m_pc ...)
    uint16_t pc;                // 12 bits
    uint16_t prevpc;            // the instruction's own address (set for the
                                // hook and for JNI; see MCS48_HOOK)
    uint8_t a;
    uint8_t psw;                // CY AC F0 BS 0 SP2 SP1 SP0
    uint8_t p1, p2;             // the ports' latches (what OUTL/ORL/ANL wrote)
    uint8_t timer;
    uint8_t prescaler;          // 5 bits: the timer counts every 32 cycles
    uint8_t t1_history;         // the counter's last T1 samples
    uint8_t a11;                // SEL MB0/MB1: 0 or 1
    uint8_t timecount_enabled;  // MCS48_TIMER_ENABLED, MCS48_COUNTER_ENABLED or 0
    bool f1;
    bool irq_state;             // the INT pin, asserted
    bool irq_polled;            // the last instruction was a JNI not taken (MAME's hack)
    bool irq_in_progress;       // from an interrupt's entry to its RETR
    bool timer_overflow;        // a timer interrupt waiting
    bool timer_flag;            // TF, for JTF
    bool tirq_enabled, xirq_enabled;
    uint8_t ram[256];           // internal RAM: 64 bytes used on an 8035
                                // (MCS48_RAM_MASK), registers at 0/24, stack at 8
    // the timeslice (device_execute_interface's)
    int icount;                 // cycles left in it
    int cycles_running;         // asked for, less any aborted
    int cycles_stolen;          // given back by mcs48_abort
    bool executing;             // inside <name>_run
    uint64_t total;             // cycles run by <name>_run since <name>_reset
    void *user;                 // the plugin's
} mcs48_t;

// power on (MAME's device_start): every register 0. Call <name>_reset after.
static inline void mcs48_init(mcs48_t *m) {
    void *user = m->user;
    memset(m, 0, sizeof *m);
    m->user = user;
}

static inline void mcs48_set_irq(mcs48_t *m, int state) { m->irq_state = state != 0; }

// abort_timeslice: the cycles left are given back, so the slice ends after
// the instruction running now (only while executing)
static inline void mcs48_abort(mcs48_t *m) {
    if (!m->executing || m->icount <= 0) return;
    m->cycles_stolen += m->icount;
    m->cycles_running -= m->icount;
    m->icount = 0;
}

static inline void mcs48_adjust_icount(mcs48_t *m, int delta) { m->icount += delta; }

// the cycles run so far in the running timeslice
static inline int mcs48_done(const mcs48_t *m) { return m->cycles_running - m->icount; }

// MAME's total_cycles(): since the reset, and while running the slice so far
static inline uint64_t mcs48_total(const mcs48_t *m) {
    return m->total + (m->executing ? (uint64_t)(int64_t)(m->cycles_running - m->icount) : 0u);
}

// the register bank in use (R0-R7)
static inline uint8_t *mcs48_regs(mcs48_t *m) { return m->ram + ((m->psw & MCS48_B_FLAG) ? 24 : 0); }

// What MCS48_NAME(x) makes, for another file that calls them:
//   MCS48_DECLARE(usb48)  ->  int usb48_run(mcs48_t *, int); ...
#define MCS48_DECLARE(p)                        \
    int p##_run(mcs48_t *m, int cycles);        \
    void p##_reset(mcs48_t *m);                 \
    void p##_burn(mcs48_t *m, int count);
