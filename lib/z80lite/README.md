# lib/z80lite

A small, fast Z80 that runs whole instructions. It has every instruction,
the documented flags and the undocumented X and Y bits, WZ, R, IM 0, 1
and 2, NMI, HALT, the EI delay and the Z80's cycle counts. It is about a
third the size of `lib/z80` and a little faster. What it doesn't do is
stop inside an instruction, so it can't match MAME cycle for cycle when
several CPUs share a bus.

| File | What |
|---|---|
| `z80lite.h` | the API, with an example |
| `z80lite.c` | the core |
| `test.c` | the test: random code beside `lib/z80`, and cases |

## Which Z80

Use `lib/z80` when the plugin has to match MAME: an arcade board whose
CPUs talk to each other mid-frame, one that needs wait states, the
acknowledge's timing or `lib/sched`, or a port you check against MAME's
trace frame by frame. Its timeslices end at the same step MAME's do.

Use `z80lite` when RAM is short or the Z80 is one part of something
bigger: a sound CPU beside the main emulator (the Neo Geo Pocket Color
plugin's), a console whose Z80 needs the right instructions and times
but not MAME's interleaving, or several Z80s, each with a different
memory map. A slice ends after the instruction that crosses it, so it
may run a few cycles over (22 at most). Take the extra off the next
slice.

## Where it came from

The Neo Geo Pocket Color plugin's sound CPU (`ngpz80.c`), written from
Zilog's Z80 user manual and Sean Young's "The Undocumented Z80
Documented", and checked there against `lib/z80` on the games' sound
drivers. Its owner released it under the MIT license for the kit.

What changed on the way here:

- Everything is in a `z80lite_t`, so a plugin can run several. The
  flag tables are shared.
- The Pocket's memory map became two windows read inline (RAM and ROM)
  and four hooks: reads, writes, IN and OUT. IN, OUT and the block I/O
  instructions go through the I/O hooks.
- The interrupt acknowledge is a hook that returns the byte on the bus,
  or `irq_data` without one. IM 0 runs an RST or EI from it, and IM 2
  reads its vector from `I * 256 +` the byte. (The Pocket's bus always
  read 0xFF.) The line is a level, held until the board clears it.
- The idle-loop skip is an option, off by default.
- The fuzz below found three places where the Pocket's copy and MAME
  differ. Each is fixed here to work as MAME's does:
  - ED 77 and ED 7F are 8 cycles (two NOP fetches, as Sean Young's
    document says), not 9.
  - SCF and CCF take X and Y from `(F & Q) | A`, where Q is 0 after an
    instruction that set the flags and 0x28 after one that didn't (a
    DD or FD prefix counts as one that didn't). They took them from A.
    The Q rule was found on real Z80s after Sean Young's document.
  - An NMI taken straight after EI ends the EI's delay, so an IRQ held
    at the RETN is taken at once. It came one instruction late.

## Using it

Add it to the plugin's sources (`lib/` is on the include path already):

```cmake
target_sources(mygame PRIVATE ${PARROTCART_DIR}/lib/z80lite/z80lite.c)
set_source_files_properties(${PARROTCART_DIR}/lib/z80lite/z80lite.c
    PROPERTIES COMPILE_OPTIONS "-O3;-fno-data-sections")
```

Its loop and instructions go in RAM on the cart
(`.time_critical.z80lite`). Set the CPU up after `z80lite_init()`:

```c
#include "z80lite/z80lite.h"

static z80lite_t snd;
static uint8_t snd_ram[0x800];

static uint8_t snd_in(z80lite_t *z, uint16_t port) { (void)z; return port & 1 ? latch : 0xFF; }
static void snd_out(z80lite_t *z, uint16_t port, uint8_t v) { (void)z; psg_write(port, v); }

z80lite_init(&snd);
snd.rom = snd_rom; snd.rom_base = 0x0000; snd.rom_size = 0x2000;
snd.ram = snd_ram; snd.ram_base = 0x4000; snd.ram_size = sizeof snd_ram;
snd.in = snd_in; snd.out = snd_out;

int debt = 0;
// each frame
debt = z80lite_run(&snd, 3579545 / 60 - debt) - (3579545 / 60 - debt);
```

### Memory

An address in the RAM window is `ram[a - ram_base]`, read and written
inline. An address in the ROM window is read inline the same way, and a
write there goes to the write hook (or nowhere). Everything else goes to
`rd` and `wr`. A missing read hook reads 0xFF, a missing write hook
drops the write. A window must not run past 0xFFFF, and size 0 is no
window.

Opcodes are looked for in the ROM window first, data in the RAM window
first. When code runs from RAM, point the ROM window at that RAM as well:
it reads the same bytes and runs about a fifth faster (the table below).
The writes still go through the RAM window.

### The API

| Call | What |
|---|---|
| `z80lite_init(z)` | power on: registers 0 but AF, SP, IX and IY (0xFFFF), no memory, no hooks, `irq_data` 0xFF |
| `z80lite_reset(z)` | the RESET line: PC, I and R 0, IM 0, interrupts off, out of a HALT, `total` 0 |
| `z80lite_run(z, cycles)` | whole instructions until at least `cycles` have passed: the cycles run |
| `z80lite_step(z)` | one instruction, or the interrupt it takes first: its cycles |
| `z80lite_set_irq(z, state)` | the INT line, a level: it stays until the board clears it |
| `z80lite_set_nmi(z, state)` | the NMI line: one NMI on each rise |
| `z80lite_abort(z)` | from a hook: `z80lite_run` returns after this instruction |
| `z80lite_r(z)` | R as LD A,R reads it |

The registers are in the struct (`z->af.b.h` is A, `z->hl.w` is HL) and
can be read any time and set between runs. `z->total` counts the cycles
since the last reset.

Hooks get the CPU, so CPUs wired the same can share them, and `z->user`
is the board's. A hook may raise or clear lines and abort any CPU, but
must not run one. Hooks can't see how far into the slice the Z80 is. A
board that needs that (a timer read mid-slice, another CPU that must see
a write at the right cycle) wants `lib/z80`, or short slices.

### Interrupts

The `ack` hook is the acknowledge: it returns the byte on the data bus
and may clear the line (MAME's HOLD_LINE). Without one the byte is
`irq_data` and the line stays up until the board clears it.

- IM 0 runs the byte: an RST (13 cycles), or EI (6). Any other byte does
  nothing, as in MAME. A CALL or JP on the bus isn't supported.
- IM 1 is RST 38h (13 cycles).
- IM 2 calls the address at `I * 256 +` the byte (19 cycles).
- An NMI goes to 0066h in 11 cycles, keeping IFF1 in IFF2 for RETN.

A HALT with nothing to wake it lets the rest of the slice pass as NOPs
at once, with R counting them.

### The idle skip

With `z->idle_skip` on, a loop that comes back round to the same jump
with the same registers and nothing written in between is waiting, and
`z80lite_run` lets the rest of the slice pass at once.
`z->idle_cycles` counts the cycles let pass. A sound driver that waits
for its next interrupt or command spends most of its time this way.

It's safe only when nothing the loop reads changes during a slice
without the board saying so. A raised IRQ or NMI ends the wait, and a
new slice starts looking again, so a command latch the board sets
between slices is fine. A hook whose value changes with time inside a
slice (a timer, a busy flag that clears after so many cycles) is not,
and nor is memory another CPU writes while this one runs. Leave it off
there. It costs nothing measurable on busy code.

## The test

`lib/test.sh` builds `test.c` with the address and undefined-behaviour
checkers and runs it, about a second:

```sh
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -I lib lib/z80lite/test.c -o /tmp/z80lite-test
/tmp/z80lite-test                  # a million instructions each way, seed 1
/tmp/z80lite-test 100000000 7      # a hundred million each way, seed 7
```

The fuzz runs random code on z80lite beside `lib/z80` (MAME 0.288's)
and compares them before every instruction: every register, the flags
with X and Y, WZ, R, IM, the IFFs, HALT, the cycle count, each write to
a hook and each OUT, with the memory every 1024 instructions and at the
end. A byte in four is a prefix (CB, DD, ED or FD), so every main and
ED opcode turns up. IRQs come now and then with a random byte on the bus
(IM 0's EI an eighth of the time), a quarter of them held after the
acknowledge, and NMIs rarer. A HALT with nothing to wake it is woken by
an NMI. z80lite runs random slices of 1 to 3000 cycles, and lib/z80
follows it from its `Z80_TRACE`. It runs three ways: everything through
the hooks; a ROM window, a hooked area and a RAM window; and code in a
RAM window with the ROM window on it. The cases check what the fuzz
can't: ports and WZ, block I/O, IM 0, 1 and 2 with and without the ack
hook, the level of the line, the EI delay, NMI edges, HALT, the windows,
abort, two CPUs, the idle skip and Q.

Before the first commit it ran 100 million instructions each way on six
seeds (1.8 billion in all) with no difference. The test was also run on
16 copies of z80lite, each with one fault planted, and it failed on
every one:

- CCF without Q, and a DD or FD prefix keeping the Q before it
- IM 0's EI without its delay, and with the delay but no look at the
  line after it
- an NMI keeping the EI delay, and RETN not looking at the lines
- an NMI of 12 cycles, OTDR one cycle long, a HALT a NOP short
- DAA after SUB, R counted twice after DD CB, block I/O's P flag
- INI reading the port after B counts down, an extra OUT in OUTI, OUT
  (n),A's WZ
- an IRQ not taking the CPU out of a HALT

## Cost on the cart

`tools/armcount` on one bench for all of them: a loop of loads, ALU
work, CB shifts, (IX+d), OUT, CALL, RET, DJNZ and LDIR, 60 lists of
60,000 Z80 cycles. (It isn't `lib/z80/README.md`'s loop, so its numbers
differ a little from there.) The sizes are the core with one CPU and
four small handlers, at `-O3 -fno-data-sections` for the M33 with the
kit's Arm GCC 9.2.1.

| Core | Code and tables | Time for 60,000 Z80 cycles |
|---|---|---|
| `lib/z80` | 103,110 bytes | 4.08 ms |
| `lib/z80`, `-DZ80_COMPACT` | 88,286 bytes | 4.25 ms |
| `lib/z80`, `-DZ80_COMPACT=2` | 70,902 bytes | 5.35 ms |
| z80lite, ROM and RAM windows | 33,868 bytes | 3.72 ms |
| z80lite, code in RAM with the ROM window on it | | 3.72 ms |
| z80lite, code in RAM, no ROM window | | 4.45 ms |

z80lite's 33,868 bytes are 32,844 of code and 1,024 of flag tables
shared by every CPU. Each `z80lite_t` is 144 bytes.
