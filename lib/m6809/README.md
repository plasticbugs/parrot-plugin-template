# lib/m6809: the Motorola 6809

MAME 0.288's 6809 in C, built for the cart: the same instructions
(the undocumented ones too), flags, cycle counts, bus cycles, interrupt
entry, SYNC and CWAI, with MAME's timeslice behaviour. A board built on it
can be checked against MAME instruction by instruction. It is a template
for the plugin's .c file to include, so the CPU's memory map is inlined
into its run loop. A board can have several (Star Wars has two, its main
CPU and its sound CPU).

## Where it came from

MAME's `src/devices/cpu/m6809` (`m6809.cpp`, `m6809.h`, `m6809inl.h`,
`base6x09.lst`, `m6809.lst`; copyright-holders Nathan Woods, BSD-3-Clause),
by way of the Robotron and Star Wars plugins' `m6809_core.h` (the same
file in both). It moved here with these fixes, all from MAME's source:

- CWAI: 16 cycles, then 4 for the vector when an interrupt comes (was
  20, then the entry's 19 or 10)
- SYNC: 2 cycles, the wait, then 1 (was 4 at once)
- EXG: MAME's order and its 8/16-bit mixing; a 16-bit register read as
  8 bits gives $FF and its low byte
- SEX keeps V
- the undocumented opcodes ($01 NEG, $02 XNC, $0B XDEC, $3E XRES, $4E and
  $5E XCLR, the free-run $14 $15 $CD, $10 $3E XSWI2, $11 $3E XFIRQ, the
  odd page 2 and 3 stores and adds) and the undefined indexed modes
- SWI2 and SWI3's dummy bus cycles; an unknown page 2 or 3 opcode falls
  back to page 1 as MAME's does
- NMI is taken only once S has been loaded since reset (LDS, LEAS or TFR
  to S, as MAME counts it)

The Robotron and Star Wars ROMs never run any of these, so those plugins
come out the same (below).

## Using it

```c
#include "m6809/m6809.h"

static m6809_t cpu;

#define M6809_NAME(x)   maincpu_##x       // maincpu_run, maincpu_reset, maincpu_step
#define M6809_RD(a)     main_rd(a)        // a byte read (a is a uint16_t)
#define M6809_WR(a, v)  main_wr(a, v)     // a byte write
#include "m6809/m6809.c"

m6809_init(&cpu);                         // power on
maincpu_reset(&cpu);                      // the RESET pin: PC from $FFFE
int ran = maincpu_run(&cpu, 1000);        // a timeslice: the cycles it ran
m6809_set_irq(&cpu, 1);                   // the lines: 1 asserted, 0 clear
```

`m6809.h` has the API and `m6809.c`'s top comment has every macro. The
state is a pointer (`m6809_t *`). Each copy of the template gets its own
`M6809_NAME`. The macros see the CPU as `c`, so CPUs wired alike can share
one copy through `c->user`.

| call | what it does (MAME's execute_run semantics) |
|---|---|
| `<name>_run(c, n)` | a timeslice of n cycles; returns the cycles run (more if the last instruction overran, fewer if aborted) |
| `m6809_abort(c)` | ends the running timeslice after this instruction (abort_timeslice) |
| `m6809_adjust_icount(c, d)` | adds d to the cycles left (negative: wait states) |
| `m6809_done(c)` | the cycles run so far in this timeslice |
| `m6809_now(c)`, `m6809_total(c)` | the cycles since power on, to the start of this instruction |
| `m6809_set_irq`, `_set_firq`, `_set_nmi` | the lines; a rise during a run ends that run, so the next instruction sees it |
| `<name>_reset(c)`, `<name>_step(c)` | the RESET pin; one instruction inside the running timeslice |

These are the calls `lib/sched` expects of a CPU: `SCHED_RUN(d, n)` is
`maincpu_run(&cpu, n)`, `SCHED_ABORT(d)` is `m6809_abort(&cpu)` and
`SCHED_DONE(d)` is `m6809_done(&cpu)`. For a HOLD_LINE, define
`M6809_IACK(line)` to call `sched_irq_ack`. MAME clocks a 6809E (MC6809E)
at its clock and an MC6809 at a quarter of it, rounded up.

Optional macros: `M6809_FETCH` and `M6809_FETCH_BASE` (opcodes from a
page pointer, worked out once an instruction), `M6809_SLOW_STEP` (an
instruction on a slow page goes to another copy built with the full bus,
such as Star Wars' slapstic), `M6809_DUMMY` and `M6809_DUMMY_VMA` (the
dummy bus cycles, for a bus that watches them), `M6809_IACK`,
`M6809_TRACE` and `M6809_HOT` (a RAM section for the run loop).

How it differs from MAME: an instruction always runs whole. MAME can stop
a 6809 partway through one at the end of a timeslice. Here the overrun
counts into the next slice, which comes to the same cycle counts.

## What it costs on the cart

About 24 KB of Thumb code a copy at -O2 (`arm-none-eabi-gcc
-mcpu=cortex-m33 -mthumb -O2`), the same as the plugins' old core. Robotron
built on this core and lib/m6800 takes 10.4 ms of each 16.77 ms list at
150 MHz on average (test/rb/armcount.sh over 300 lists: Unicorn, the
plugin's own flags, the whole board). Its old cores took 11.6 ms.

## How it was proven

The smoke test, hand-assembled instructions with no ROM, checks
registers, flags, cycles, interrupts, SYNC, CWAI and timeslices:

```sh
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-missing-field-initializers \
    -I lib -o /tmp/m6809-test lib/m6809/test.c && /tmp/m6809-test
```

The Robotron plugin built against this folder (its `m6809.h` and
`m6809_core.h` gone, `-I` to the kit's `lib`) matches MAME 0.288 on its own
tests (`RB_ROMS` is the robotron.zip; MAME runs headless):

```sh
test/rb/compare.sh        # the RAM, palette and CMOS at every frame, 7200 frames
test/rb/trace.sh          # every main CPU instruction's registers and cycle count, 3 s
```

It also comes out byte for byte the same as the plugin's own core over
those 7200 frames.
