# lib/m6800: the Motorola 6800, 6802 and 6808

MAME 0.288's 6800 in C, built for the cart: the same instructions,
flags, cycle counts, interrupt entry and WAI, with MAME's timeslice
behaviour, so a board built on it can be checked against MAME instruction
by instruction. It is a template for the plugin's .c file to include, so
the CPU's memory map is inlined into its run loop. A board can have
several. The 6801/6803/63701 family's extra instructions, ports and timer
aren't here.

## Where it came from

MAME's `src/devices/cpu/m6800` (`m6800.cpp`, `m6800.h`, `6800ops.hxx`;
copyright-holders Aaron Giles, BSD-3-Clause), by way of the Robotron
plugin's sound board, where it ran the 6808 inside `rb.c`. Moving it here
changed two things:

- an undefined opcode ($02, $61, $71 and the like) costs its 4 cycles and
  steps over 0, 1 or 2 bytes without touching memory, as MAME's do (the
  old core read memory for the indexed and extended ones)
- cycles come off the count in MAME's order: the instruction that TAP or
  CLI runs inside itself is charged before an interrupt is taken, and an
  interrupt's entry is charged after its vector is read

## MAME's ways, which a board has to keep to match it

- A 6802 or 6808 divides its clock by 4, rounding up, so on a 3,579,545 Hz
  crystal it runs 894,887 cycles a second (`m6800_cycles_per_second(hz, 4)`).
  A 6800 runs at its clock.
- The IRQ and NMI lines are looked at only when a timeslice starts and
  after CLI, TAP, RTI and WAI. A slice that starts by taking an interrupt
  still runs one instruction after it.
- TAP, and CLI when it clears I, run the next instruction inside
  themselves before an interrupt can be taken.
- WAI costs 9 cycles. While it waits, a timeslice goes by whole. Coming
  out costs 4 (the state is already stacked).

## Using it

```c
#include "m6800/m6800.h"

static m6800_t snd;

#define M6800_NAME(x)   sndcpu_##x        // sndcpu_run, sndcpu_reset, sndcpu_step
#define M6800_RD(a)     snd_rd(a)         // a byte read (a is a uint16_t)
#define M6800_WR(a, v)  snd_wr(a, v)      // a byte write
#define M6800_LOOP_SKIP                   // optional: delay loops at once (not in trace builds)
#include "m6800/m6800.c"

m6800_init(&snd);                         // power on
sndcpu_reset(&snd);                       // the RESET pin: PC from $FFFE, I set
int ran = sndcpu_run(&snd, 500);          // a timeslice: the cycles it ran
m6800_set_irq(&snd, 1);                   // the lines: 1 asserted, 0 clear
```

`m6800.h` has the API and `m6800.c`'s top comment has every macro. The
state is a pointer (`m6800_t *`). Each copy of the template gets its own
`M6800_NAME`. The macros see the CPU as `c`, so CPUs wired alike can share
one copy through `c->user`.

| call | what it does (MAME's execute_run semantics) |
|---|---|
| `<name>_run(c, n)` | a timeslice of n cycles; returns the cycles run (more if the last instruction overran, fewer if aborted) |
| `m6800_abort(c)` | ends the running timeslice after this instruction (abort_timeslice) |
| `m6800_adjust_icount(c, d)` | adds d to the cycles left (negative: wait states) |
| `m6800_done(c)` | the cycles run so far in this timeslice |
| `m6800_now(c)`, `m6800_total(c)` | the cycles since power on, to the start of this instruction |
| `m6800_set_irq`, `m6800_set_nmi` | the lines; a rise during a run ends that run, as MAME's synchronised set_input_line does |
| `<name>_reset(c)`, `<name>_step(c)` | the RESET pin; one instruction inside the running timeslice |

These are the calls `lib/sched` expects of a CPU: `SCHED_RUN(d, n)` is
`sndcpu_run(&snd, n)`, `SCHED_ABORT(d)` is `m6800_abort(&snd)` and
`SCHED_DONE(d)` is `m6800_done(&snd)`, with `sched_cpu(d, hz, 4, 1)` for a
6802 or 6808. For a HOLD_LINE, define `M6800_IACK(line)` to call
`sched_irq_ack`.

`M6800_LOOP_SKIP` runs a delay loop (DECA, DECB or DEX, then BNE back to
it) all but its last turns at once, as far as the timeslice goes. Nothing
else can happen inside a 6800 timeslice, so the CPU comes out exactly as
it would running every turn. The loop's bytes have to read without side
effects. Robotron's sound ROM spends a third of its instructions in these
loops.

## What it costs on the cart

About 12 KB of Thumb code a copy at -O2 (`arm-none-eabi-gcc
-mcpu=cortex-m33 -mthumb -O2`). Robotron built on this core and lib/m6809
takes 10.4 ms of each 16.77 ms list at 150 MHz on average, against 11.6 ms
with its old cores (test/rb/armcount.sh over 300 lists: Unicorn, the
plugin's own flags, the whole board). Most of the gain is the delay-loop
shortcut, which now skips every turn the slice allows.

## How it was proven

The smoke test, hand-assembled instructions with no ROM, checks
registers, flags, cycles, IRQ, NMI, WAI, TAP and CLI, timeslices, and the
loop shortcut against running every turn at 40 slice lengths:

```sh
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-missing-field-initializers \
    -I lib -o /tmp/m6800-test lib/m6800/test.c && /tmp/m6800-test
```

The Robotron plugin built against this folder (its sound CPU now
`m6800_t` and `sndcpu_run`, `-I` to the kit's `lib`) matches MAME 0.288
(`RB_ROMS` is the robotron.zip; MAME runs headless):

```sh
RB_CPU=sound test/rb/trace.sh   # every sound CPU instruction's registers and cycle count, 3 s
test/rb/compare.sh              # the main board at every frame, 7200 frames
test/rb/soundcmp.sh             # the DAC against MAME's -wavwrite
test/rb/loopcheck.sh            # M6800_LOOP_SKIP against every instruction, 5400 frames
```

Its sound CPU's registers, cycles and RAM at every frame, and every DAC
write, also come out byte for byte the same as the plugin's own core over
7200 frames.
