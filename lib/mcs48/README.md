# lib/mcs48: the Intel 8035 (MCS-48)

The MCS-48 family's CPU (8035, 8039, 8048 and the like) as MAME 0.288 runs
it, for an arcade board's sound CPU. It has the same instructions, cycle
counts, timer and counter, interrupts (with MAME's JNI quirk), ports, BUS,
MOVX and 8243 expander operations, and the same timeslice behaviour. A
board built with it can be checked against MAME instruction by
instruction and frame by frame.

- `mcs48.h`: the state (`mcs48_t`, one per CPU) and the functions every
  copy shares.
- `mcs48.c`: the instructions, a template. Your `.c` file defines the
  CPU's connections and includes it, so the port reads and writes inline.
- `test.c`: a smoke test.

## Where it came from

It's a C port of MAME's `src/devices/cpu/mcs48/mcs48.cpp` (tag `mame0288`;
copyright holders Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud;
BSD-3-Clause). Two Parrot Cart plugins had their own copies: Star Trek
(`src/mcs48.inc`, two 8035s on one board) and Donkey Kong Jr.
(`src/mcs48_core.h`). This one is Star Trek's, which follows MAME most
closely, with its state moved into `mcs48_t` and these additions:

- the bus (`OUTL BUS`, `ORL BUS`, `ANL BUS`), the PROG pin around an
  expander operation, and the bus floated at reset;
- every connection can see the time, end the slice or add wait states,
  not only the port writes;
- RAM sizes other than 64 bytes (`MCS48_RAM_MASK`);
- a hook before instructions, for a trace or a fast path (Donkey Kong
  Jr.'s music loop runs in it).

## Using it

```c
#include "mcs48/mcs48.h"

static mcs48_t snd;                        // the sound CPU
static uint8_t snd_rom[0x1000], dac, latch;

#define MCS48_ROM          snd_rom         // required: the program memory
#define MCS48_P1_W(d)      (dac = (d))     // port 1's latch, written
#define MCS48_EXT_R(a)     latch           // MOVX A,@Rr
#define MCS48_T1()         ((latch >> 4) & 1)
#include "mcs48/mcs48.c"                   // mcs48_run(), mcs48_reset(), mcs48_burn()

void board_reset(void) { mcs48_init(&snd); mcs48_reset(&snd); }
void board_line(void)  { mcs48_run(&snd, 25); }   // 25 machine cycles (15 clocks each)
```

The connections, all optional except `MCS48_ROM`. Each can use `m`, the
`mcs48_t *` of the CPU that's running:

| Macro | What | Default |
|---|---|---|
| `MCS48_ROM` | program memory, a `const uint8_t *` (read at each run and after each write, so a port can switch its bank) | required |
| `MCS48_ROM_MASK` | its address mask: `0xfff` 4K, `0x7ff` 2K mirrored | `0xfff` |
| `MCS48_RAM_MASK` | internal RAM: `0x3f` an 8035's 64 bytes, `0x7f` an 8039's | `0x3f` |
| `MCS48_P1_R()`, `MCS48_P2_R()` | the ports' pins (`IN A,Pp` ANDs them with the latch) | `0xff` |
| `MCS48_P1_W(d)`, `MCS48_P2_W(d)` | the ports' latches, written | nothing |
| `MCS48_BUS_R()`, `MCS48_BUS_W(d)` | `INS A,BUS`; `OUTL`, `ORL`, `ANL BUS` | `0xff`, nothing |
| `MCS48_T0()`, `MCS48_T1()` | the test pins, 0 or 1 | 0 |
| `MCS48_EXT_R(a)`, `MCS48_EXT_W(a, d)` | `MOVX` | 0, nothing |
| `MCS48_PROG_W(s)` | PROG, 0 then 1 around `MOVD`, `ORLD`, `ANLD` | nothing |
| `MCS48_NAME(x)` | the functions' names | `mcs48_##x` |
| `MCS48_API` | their storage class (`static` keeps them in the file) | extern |
| `MCS48_HOOK(m)`, `MCS48_HOOK_AT(pc)` | a hook before an instruction (below) | none |

A connection is called with the cycle count up to date. It can read the
time (`mcs48_total`), end the slice (`mcs48_abort`), add wait states
(`mcs48_adjust_icount`) or set the CPU's INT line. Each instruction
burns its cycles before it acts, as MAME's do, so a port write is seen at
the end of its instruction.

### The functions

`m` is a pointer to the CPU's state, so one copy of the code serves any
number of CPUs wired the same way. CPUs wired differently need a copy
each: include `mcs48.c` again, from the same file or another, after their
own connections and a different `MCS48_NAME`.

| Function | What (MAME's name) |
|---|---|
| `int <name>_run(m, cycles)` | `execute_run` for a timeslice: instructions while cycles are left (the last can overrun). Returns the cycles run: more than asked if it overran, fewer if aborted. |
| `void <name>_reset(m)` | `device_reset`, with the total cycles to 0 first, as MAME's reset does. It writes the bus and both ports (`0xff`). |
| `void <name>_burn(m, n)` | `burn_cycles`: n cycles go by on the timer or counter and the slice. For a hook. |
| `mcs48_init(m)` | `device_start`: every register 0 (`m->user` kept). Reset after it. |
| `mcs48_abort(m)` | `abort_timeslice`: the cycles left are given back, and the slice ends after this instruction |
| `mcs48_adjust_icount(m, d)` | `adjust_icount` (negative: wait states) |
| `mcs48_done(m)` | cycles run so far in this timeslice |
| `mcs48_total(m)` | `total_cycles()`: since the reset, this slice's so far too |
| `mcs48_set_irq(m, state)` | the INT pin (`MCS48_INPUT_IRQ`): 1 asserted, 0 clear |
| `mcs48_regs(m)` | the register bank in use, R0-R7 |
| `MCS48_DECLARE(prefix)` | the prototypes of a copy's functions, for another file |

The registers are fields of `mcs48_t` (`pc`, `a`, `psw`, `p1`, `p2`,
`timer`, `ram[]` ...). Between runs they are all current. MAME's Lua
state has no PSW, so a test against MAME's Lua dump can't compare it. The
PSW's bit 3 is kept 0 and reads as 1 (`MOV A,PSW`), as in MAME.

### With lib/sched

```c
#define SCHED_CPUS 2
#define SCHED_RUN(d, n)  ((d) == 0 ? z80_run(0, n) : snd48_run(&snd, n))
#define SCHED_ABORT(d)   ((d) == 0 ? z80_abort(0) : mcs48_abort(&snd))
#define SCHED_DONE(d)    ((d) == 0 ? z80_done(0) : mcs48_done(&snd))
#define SCHED_RESET(d)   ((d) == 0 ? z80_reset(0) : snd48_reset(&snd))
#include "sched/sched.c"
...
sched_cpu(1, 6000000, 15, 1);              // a 6 MHz 8035: 15 clocks a cycle, at least 1 a slice
static void snd_irq(int state) { mcs48_set_irq(&snd, state); }
sched_line(0, 1, SCHED_LINE_NORMAL, 0, snd_irq);
```

### The hook

`MCS48_HOOK(m)` is called before an instruction whose address passes
`MCS48_HOOK_AT(pc)` (default: every one), with `*m` up to date: `m->pc`
is the instruction's address, the timer is current and `m->icount` is
the cycles left. It returns 0 to let the instruction run. To skip ahead,
it does the work of instructions itself, changing `*m` exactly as they
would and burning their cycles with `<name>_burn`, and returns nonzero.
Donkey Kong Jr.'s sound program spends most of its time in two loops of
34 cycles. Its hook runs whole turns of them while a turn fits in the
slice (`m->icount > 32`), and the board still matches MAME. A trace is a
hook that prints and returns 0.

## The smoke test

From the kit's top folder:

```sh
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-missing-field-initializers -Ilib lib/mcs48/test.c -o /tmp/mcs48test && /tmp/mcs48test
```

It runs small hand-made programs and checks their results and cycle counts
as MAME's 8035 gives them. Most run in one slice and again in slices of
every size from 1 cycle up. The checks cover the arithmetic and flags,
CALL and RET with the PSW on the stack, SEL MB1, the timer and its
interrupt with RETR, the counter's falling edges on T1, the INT pin and
MAME's JNI quirk, MOVX, the bus, MOVD and the PROG pin, wait states, an
aborted slice, and a hook that runs a loop itself. It prints `mcs48: all
passed`, or what failed and exits 1. It also builds without warnings for
the cart (`arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -O2 -Wall -Wextra
-std=c11 -Ilib -c lib/mcs48/test.c`).

## How fast

The Star Trek plugin's two 8035s, one at 400,000 cycles a second and one
at 208,000, take 442,000 M33 cycles of each 16.77 ms list: 2.9 ms at the
cart's 150 MHz, 18% of the time. That's about 40 M33 cycles for each 8035
cycle on the first and 49 on the second, which spends its time polling
the SP0250. The plugin's own copy took 4% less. These figures come from
its `test/st/armcount.sh` (Unicorn with the M33's book cycle times) over
600 lists of a game played by its bot, with `-O3`.

## How it was proven

Each plugin was built against `lib/` with its own core replaced by this
one (copies of the plugin, outside its repository), and its tests
against MAME 0.288 were run, headless:

- **Star Trek** (`ST_ROMS`: MAME's `startrek.zip`). Its `st48usb.c` and
  `st48sp.c` became two copies of this template, `usb48_` and `sp48_`.
  - `test/st/compare.sh attract`: the main RAM, the vector RAM, the sound
    board's RAMs, and all three CPUs' registers and RAM matched at every
    frame, 4,000 of 4,000.
  - `test/st/compare.sh game`, a game played by its bot: 8,000 of 8,000.
  - `test/st/trace.sh usb 10` and `trace.sh speech 10`: 1,988,666 and
    1,051,725 instructions the same as MAME's, registers and cycle counts.
  - The board's dump and its sound writes over the 8,000-frame game were
    byte for byte the plugin's own.
- **Donkey Kong Jr.** (`test/jr/check.sh ROMDIR`, ROMDIR holding MAME's
  `dkongjr.zip`; `MAME_ROMPATH` for the ColecoVision BIOS). Its
  `src/jr.c` included this template, with its music loop as a hook.
  - All four steps passed. In step 1, the Z80's RAM and the 8035 matched
    MAME at every one of 6,000 frames.
  - With its `--m48trace` (and `JR_M48TRACE` in MAME's dump script),
    1,569,241 instructions from frame 100 to 400 were the same as MAME's,
    registers, P2, PC and cycle counts.
  - Its `cmptrace48.py` reads MAME's bank prefix (`1:740` is $F40)
    wrongly and stops at the first SEL MB1. It needs the address taken
    as bank * $800 + the rest.
  - The sound differs from the plugin's own only in its first 2 seconds.
    MAME's reset writes $FF to port 1, the DAC, and the plugin's copy
    didn't.
