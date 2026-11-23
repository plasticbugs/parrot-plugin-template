# lib/sched

MAME 0.288's device scheduler in C, for a board with any number of CPUs
of any kind. It covers timeslices up to the next timer, the quantum and
the perfect quantum (`boost_interleave`), timers with callbacks,
`synchronize`, `abort_timeslice`, suspend and resume, and input lines set
through MAME's event queue. The CPUs run the same cycles as MAME's, in
the same order, so a board built on it can match MAME frame by frame.

| File | What |
|---|---|
| `sched.h` | the API, and the CPU interface at the top |
| `sched.c` | the scheduler, a template the board's .c file includes |
| `test.c` | the smoke test |

## Where it came from

- MAME 0.288: `src/emu/schedule.cpp` (`device_scheduler`, `emu_timer`)
  and `src/emu/diexec.cpp` (`device_execute_interface`: suspend, eat
  cycles, input lines and their event queue), by Aaron Giles.
  BSD-3-Clause. The files keep MAME's license and copyright-holders lines.
- The C version is the one the Star Trek plugin (`parrot-plugin-startrek`,
  `src/st.c`) and the Time Pilot plugin (`src/tp.c`) each wrote for their
  own board, made general: N CPUs, timers by callback, input lines by
  table, quanta as a list.

## The CPU interface

A core plugs in with MAME's `execute_run` semantics. `lib/z80`,
`lib/mcs48`, `lib/m6809` and `lib/m6800` follow it:

```
int  <cpu>_run(cpu, cycles)           a timeslice; returns cycles - icount left - cycles an abort took back
void <cpu>_abort(cpu)                 abort_timeslice: icount > 0 is taken back
int  <cpu>_done(cpu)                  cycles run so far in this slice
void <cpu>_adjust_icount(cpu, delta)  from a handler
uint64_t <cpu>_total(cpu)             cycles since its reset
void <cpu>_set_irq(cpu, state), <cpu>_set_nmi(cpu, state)   1 ASSERT, 0 CLEAR
void <cpu>_reset(cpu)                 device_reset
```

The scheduler itself calls only run, abort and done, plus reset for a
RESET line. The board names them in macros before it includes `sched.c`,
so the calls inline. A board with several kinds of CPU switches on `d`.

## Using it

```c
#include "sched/sched.h"
#include "z80/z80.h"

enum { D_MAIN, D_SOUND, D_N };          // MAME's device order
#define SCHED_CPUS D_N
#define SCHED_RUN(d, n)  ((d) == D_MAIN ? z80_run(0, n) : snd48_run(&snd, n))
#define SCHED_ABORT(d)   ((d) == D_MAIN ? z80_abort(0) : mcs48_abort(&snd))
#define SCHED_DONE(d)    ((d) == D_MAIN ? z80_done(0) : mcs48_done(&snd))
#define SCHED_RESET(d)   do { if ((d) == D_SOUND) snd48_reset(&snd); } while (0)
#include "sched/sched.c"

enum { IN_MAIN_IRQ, IN_SND_RESET };
static int tm_vblank;
static void main_irq(int state) { z80_set_irq(0, state); }
static void vblank(int param) {
    (void)param;
    sched_set_input(IN_MAIN_IRQ, SCHED_HOLD_LINE);
    sched_timer_adjust(tm_vblank, SCHED_HZ(60), 0, SCHED_NEVER);
}
static void sound_latch_w(int param) { latch = (uint8_t)param; }

void board_start(void) {
    sched_init();
    sched_cpu(D_MAIN, 3072000, 1, 1);       // clock, clocks a cycle, execute_min_cycles
    sched_cpu(D_SOUND, 6000000, 15, 1);
    sched_line(IN_MAIN_IRQ, D_MAIN, SCHED_LINE_NORMAL, 0xff, main_irq);
    sched_line(IN_SND_RESET, D_SOUND, SCHED_LINE_RESET, 0, NULL);
    tm_vblank = sched_timer_alloc(vblank);
    sched_timer_adjust(tm_vblank, SCHED_HZ(60), 0, SCHED_NEVER);
    // the machine's reset, in MAME's order
    for (int d = 0; d < D_N; d++) sched_reset_cpu(d);
    z80_reset(0);
    snd48_reset(&snd);
}

// in a Z80 handler: the write MAME's driver makes through synchronize()
//     sched_synchronize(sound_latch_w, d);
//     sched_perfect_quantum(SCHED_USEC(100));
// and the vector, with HOLD_LINE let go:
//     #define Z80_IRQ_VECTOR(cpu) sched_irq_ack(IN_MAIN_IRQ)

void board_frame(uint64_t frame) {
    sched_run_to(frame * 3072000 / 60, 3072000);   // or loop sched_timeslice() until a flag
}
```

A time is MAME's attotime as one 64-bit number, `sched_time_t`:
attoseconds from the epoch, a whole number of seconds (`sched_epoch()`)
since power on. The epoch moves on a second at a time, between
timeslices. Every time the scheduler holds moves with it, and so does any
time the board hands to `sched_keep()`, such as a VBLANK's start. A delay
must be under about 8 seconds, or `SCHED_NEVER`. `sched_ticks(t, hz)`
turns a time into ticks from power on.

What's in `sched.h`:

- Power on: `sched_init`, then `sched_cpu(d, clock, divider, min_cycles)`
  for each CPU in MAME's device order. Each CPU starts suspended for
  RESET, as MAME's do until the machine resets, and `sched_reset_cpu(d)`
  frees it. `sched_max_quantum` and `sched_perfect_cpu` are the machine
  config's `set_maximum_quantum` and `set_perfect_quantum`.
- Running: `sched_timeslice`, `sched_run_to(tick, hz)`, `sched_now`
  (`machine().time()`), `sched_base`, `sched_local_time`,
  `sched_total_cycles`, `sched_executing`, `sched_abort`,
  `sched_suspend` / `sched_resume` / `sched_suspended` / `sched_yield`,
  and `sched_add_quantum` / `sched_perfect_quantum`.
- Timers: `sched_timer_alloc(cb)`, `sched_timer_adjust(t, delay, param,
  period)`, `sched_timer_adjust_at`, `sched_timer_enable`, and the
  getters. `sched_timer_set` and `sched_synchronize` make temporary
  one-shots. Timers due at the same moment fire in the order they were
  set. In a callback, `sched_now()` is the timer's expiry.
- Input lines: `sched_line(line, d, kind, default vector, set)`, then
  `sched_set_input`, `sched_set_input_and_vector`,
  `sched_set_input_vector`, `sched_input_state` and `sched_irq_ack`.
  Changes are queued (32 a line) and applied by a synchronize, as MAME
  does. A RESET line holds its CPU suspended, eating its cycles, and when
  let go it calls `sched_reset_cpu(d)` and `SCHED_RESET(d)` and resumes
  it. A HALT line suspends it.

Optional sizes, defined before including `sched.c`: `SCHED_TIMERS` 32
(the board's timers plus the temporary ones outstanding at once),
`SCHED_LINES` 8, `SCHED_QUANTA` 8, `SCHED_KEEPS` 8. A full table calls
`SCHED_OVERFLOW()`, `__builtin_trap()` by default. On the cart the
timeslice and timer code go in `.time_critical.sched` (RAM).

Rules:

- `divider` is MAME's `execute_clocks_to_cycles` (an 8035 15, a Z80 1),
  and `min_cycles` is `execute_min_cycles` (1 for a Z80 and an 8035).
  Take both from the core's MAME header. They set the cycle length, the
  local times and the perfect interleave, so a wrong one shows up as a
  timing mismatch with MAME.
- The perfect quantum is the second smallest `minimum_quantum` (cycle
  length times `min_cycles`), as in MAME. A quantum's end is only looked
  at when a timeslice starts.
- Only one file includes `sched.c`. Other files may include `sched.h` and
  call it.

## The smoke test

From the kit's top folder:

```sh
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-missing-field-initializers lib/sched/test.c -o schedtest && ./schedtest
```

It uses two toy CPUs (1 MHz and 2 MHz, one cycle an instruction), a
10 kHz timer and a 50 Hz one. It checks a write's synchronize at the
writing CPU's cycle, with the other CPU caught up; the slice lengths
before, during and after a perfect quantum; a HOLD_LINE IRQ acknowledged
with its vector and let go; a RESET line held for 20 ms (cycles eaten,
one reset, the CPU resumed); and 2.5 s of running across two epochs, with
a kept time moved along. It prints `lib/sched: ok` and exits 0, or lists
what failed and exits 1.

## Cost on the cart

Star Trek's `armcount.sh` (the plugin built for the Cortex-M33 with its
own flags and run on Unicorn, 1200 lists of play, 150 MHz):

| Star Trek | Cycles a list | Average | 99th percentile | Worst |
|---|---|---|---|---|
| its own Z80 and scheduler | 1,820,324 | 12.1 ms | 14.0 ms | 14.7 ms |
| lib/z80, its own scheduler | 1,818,320 | 12.1 ms | 14.0 ms | 14.7 ms |
| lib/z80 and lib/sched | 1,841,831 | 12.3 ms | 14.1 ms | 14.9 ms |

The general scheduler costs about 1.2% more than the one Star Trek wrote
for its own board (about 0.14 ms a list), from timers called through
pointers and the list of quanta.

## How it was proven

Star Trek was moved onto `lib/sched` and `lib/z80` together, keeping its
own 8035 and SP0250 code, in a scratch copy outside the plugin repo and
the kit. MAME ran headless, as the plugin's scripts run it (`-video none
-videodriver dummy -window -nomaximize -sound none -nothrottle
-skip_gameinfo -seconds_to_run`, with `SDL_VIDEODRIVER=dummy`). Its
scheduler section in `st.c` (`exec_t`, the timers, `synchronize`, the
input lines, `timeslice`, `rebase` and `st_run_to`) was replaced by:

```c
sched_init();
sched_cpu(D_MAIN, ST_CLOCK, 1, 1);                // the Z80, 3.86712 MHz
sched_cpu(D_USB, 6000000, 15, 1);                 // the USB's 8035
sched_cpu(D_SP, 3120000, 15, 1);                  // the speech 8035
sched_line(IN_MAIN_IRQ, D_MAIN, SCHED_LINE_NORMAL, 0xff, main_irq);
sched_line(IN_USB_RESET, D_USB, SCHED_LINE_RESET, 0, NULL);
sched_line(IN_SP_IRQ, D_SP, SCHED_LINE_NORMAL, 0, sp_irq);
// six timers by callback; sched_keep(&vblank_start_time), sched_keep(&draw_end_time);
// the USB's data write: sched_synchronize(cb_usb, d); sched_perfect_quantum(SCHED_USEC(250));
```

```sh
export TMPDIR=/tmp/lib-proof KIT=<the kit> ST_ROMS=<your startrek.zip>
rsync -a --exclude 'build*' --exclude .git <parrot-plugin-startrek>/ $TMPDIR/startrek/
# src/stz80.c: lib/z80's glue (lib/z80/README.md); src/st.c: the scheduler as above
cd $TMPDIR/startrek
sed -i.bak 's|-I"$src"|-I"$src" -I"$KIT/lib"|' test/st/*.sh
sh test/st/compare.sh attract
sh test/st/compare.sh game
for c in main usb speech; do sh test/st/trace.sh $c 4; done
```

| Test | Compared | Result |
|---|---|---|
| `compare.sh attract` | 4000 frames: RAM, vector RAM, USB program and work RAM, all three CPUs' registers and RAM | all identical |
| `compare.sh game` | 8000 frames of play | all identical |
| `trace.sh main 4` | 898,999 Z80 instructions from cycle 0, registers and cycle counts | all the same |
| `trace.sh usb 4` | 788,666 USB 8035 instructions from cycle 0 | all the same |
| `trace.sh speech 4` | 419,093 speech 8035 instructions | all the same |
