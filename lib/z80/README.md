# lib/z80

The Zilog Z80 as MAME 0.288 runs it, in C. One copy of the code runs one
Z80 or several. When a timeslice runs out, an instruction stops at the
same step MAME's would and carries on from there in the next slice. So a
board built on it matches MAME cycle for cycle, and `lib/sched` can run it
next to other CPUs as MAME does.

| File | What |
|---|---|
| `z80.h` | the API |
| `z80.c` | the core, a template the board's .c file includes |
| `z80_ops.h` | the instructions, made from MAME's `z80.lst` by `z80gen.py` |
| `z80gen.py` | the generator |
| `test.c` | the smoke test |

## Where it came from

- MAME 0.288, `src/devices/cpu/z80`: `z80.cpp` and `z80.h` by Juergen
  Buchmueller, and `z80.lst` by Juergen Buchmueller and Andrei I. Holub.
  BSD-3-Clause. Every file here keeps MAME's license and copyright-holders
  lines.
- The C version came from the Time Pilot plugin (`parrot-plugin-timeplt`,
  `src/z80.c` and `tools/tp/z80gen.py`), which had it from the Donkey Kong
  plugin. The Star Trek plugin (`parrot-plugin-startrek`) changed its copy
  for the Sega G80 bus.

What changed on the way here:

- Every opcode fetch goes through the opcode read, including the fast
  path at an instruction's start, so M1 wait states (`Z80_M1_WAIT`) apply
  to every fetch. Star Trek found that the fast path read opcodes with no
  M1 wait, which loses the G80's extra cycles on each fetch.
- The interrupt acknowledge calls `Z80_IRQACK` (MAME's `irqack_cb`) and
  then `Z80_IRQ_VECTOR`, both with the cycle count synced.
- Every handler sees the cycle count as it is at the access, and a change
  it makes (`z80_adjust_icount`, `z80_abort`) holds.
- Wait states for memory reads, writes, IN and OUT, and `Z80_OPCODE` and
  `Z80_WR_PAGED` for boards that watch fetches or catch some writes.
- `z80_start`, `z80_reset` and `z80_set_regs` work on a CPU while another
  one is running, so one Z80's handler can reset another.
- Per-CPU total cycles (`z80_total`), so `z80_set_nmi` needs no extra
  argument.

Left out: the NSC800, R800, Z80N and T6A84 variants, the WAIT line, the
refresh and no-MREQ callbacks, and the daisy chain. A board can build a
daisy chain from `Z80_IRQ_VECTOR` and `Z80_RETI`.

## Using it

The board's .c file defines how the CPU reaches its bus, then includes
`z80.c`. Every access inlines into the core.

```c
#include "z80/z80.h"

#define Z80_CPUS 1
#define Z80_RD(cpu, a)        board_rd(a)       // a read from a NULL page
#define Z80_WR(cpu, a, d)     board_wr(a, d)    // a write to a NULL page
#define Z80_IN(cpu, port)     board_in(port)
#define Z80_OUT(cpu, port, d) board_out(port, d)
#include "z80/z80.c"

void board_start(void) {
    for (int p = 0; p < 0x80; p++) z80_rd_page[0][p] = rom + (p << 8);   // ROM: read only
    for (int p = 0x80; p < 0x88; p++) z80_rd_page[0][p] = z80_wr_page[0][p] = ram + ((p - 0x80) << 8);
    z80_start(0);
    z80_reset(0);
}

void board_frame(void) {
    z80_run(0, CYCLES_PER_FRAME);   // or let lib/sched run it
}
```

Each CPU has its own page tables, `z80_rd_page[cpu]` and `z80_wr_page[cpu]`:
256 pointers to 256-byte pages. An access to a page that is set goes
straight to memory. A NULL page calls the board's `Z80_RD` or `Z80_WR`.
Only one file includes `z80.c`.

### The board's connections

All are optional except `Z80_CPUS`. `cpu` is 0 to `Z80_CPUS - 1`.

Handlers. These are called with the cycle count synced, so they may call
`z80_done`, `z80_total`, `z80_abort` and `z80_adjust_icount`:

| Macro | Default | When |
|---|---|---|
| `Z80_RD(cpu, a)` | 0 | a read from a NULL page |
| `Z80_WR(cpu, a, d)` | nothing | a write to a NULL page, or one `Z80_WR_PAGED` sends here |
| `Z80_RD_OP(cpu, a)` | `Z80_RD` | an opcode fetch from a NULL page of `z80_op_page` (with `Z80_OPCODES`) |
| `Z80_IN(cpu, port)` | 0 | IN (the 16-bit port, as the Z80 puts it out) |
| `Z80_OUT(cpu, port, d)` | nothing | OUT |
| `Z80_IRQ_VECTOR(cpu)` | 0xff | the acknowledge's vector (`standard_irq_callback`); IM 0 may return a 3-byte CALL as 0xcdXXXX |
| `Z80_IRQACK(cpu)` | none | `irqack_cb`, before the vector |
| `Z80_RETI(cpu)` | none | RETI (`daisy_call_reti_device`) |
| `Z80_BUSACK(cpu, state)` | none | `busack_cb` |
| `Z80_TRACE(cpu)` | none | before each instruction (`debugger_instruction_hook`); `z80_regs` and `z80_total` read as MAME's trace shows them |

Inline, with no sync. These must not call the cycle functions:

| Macro | Default | What |
|---|---|---|
| `Z80_HALT(cpu, state)` | none | `halt_cb` |
| `Z80_M1_WAIT(cpu, a)` | 0 | extra cycles for an opcode fetch at `a` |
| `Z80_RD_WAIT(cpu, a)`, `Z80_WR_WAIT(cpu, a)` | 0 | extra cycles for any other memory access |
| `Z80_IN_WAIT(cpu, port)`, `Z80_OUT_WAIT(cpu, port)` | 0 | extra cycles for I/O |
| `Z80_OPCODE(cpu, a, op)` | nothing | after each opcode fetch, with its byte |
| `Z80_WR_PAGED(cpu, a)` | 1 | 0 sends this write to `Z80_WR` though its page is set |
| `Z80_M1_CYCLES`, `Z80_MREQ_CYCLES`, `Z80_IORQ_CYCLES` | 4, 3, 4 | `z80_set_m1_cycles` and the rest |
| `Z80_OPCODES` | off | opcode fetches read `z80_op_page` (MAME's `AS_OPCODES`, for decrypted opcodes) |

The waits work like MAME's taps that call `adjust_icount(-n)`: they are
taken after the access. A wait that is 0 at compile time costs nothing.

### The API

```c
void z80_start(int cpu);                    // device_start: power on
void z80_reset(int cpu);                    // device_reset; total cycles to 0
int  z80_run(int cpu, int cycles);          // a timeslice; returns the cycles run
void z80_abort(int cpu);                    // abort_timeslice
void z80_adjust_icount(int cpu, int delta); // from a handler: < 0 takes cycles
void z80_eat_cycles(int cpu, int cycles);
int  z80_done(int cpu);                     // cycles run so far in this slice
int  z80_icount(int cpu);
uint64_t z80_total(int cpu);                // since its reset, this slice's so far too
void z80_set_irq(int cpu, int state);       // 1 ASSERT_LINE, 0 CLEAR_LINE
void z80_set_nmi(int cpu, int state);       // pending on a rising edge once it has run
void z80_set_busreq(int cpu, int state);
bool z80_busack(int cpu);
bool z80_halted(int cpu);
void z80_regs(int cpu, z80_regs_t *r);      // registers as MAME's state shows them
void z80_set_regs(int cpu, const z80_regs_t *r);
```

`z80_run` returns `cycles - icount left - cycles an abort took back`, as
MAME's `execute_run` does. HOLD_LINE belongs to the board or to
`lib/sched`: clear the line in `Z80_IRQ_VECTOR` (or call `sched_irq_ack`).

### Rules

- The connection macros expand inside `z80.c`, where the registers and
  flags are macros: `A F B C D E H L AF BC DE HL IX IY HX LX HY LY SP PC
  WZ I R R2 Q CF NF PF VF HF ZF SF` and others (`RD`, `WR`, `TDAT`...).
  Don't write those names in a connection macro. Call a function or use a
  variable with another name instead, as the examples below do. `z80.c`
  #undefs all of them at its end.
- `z80_run` doesn't nest. A handler must not run another Z80, but it may
  set another one's lines, read its registers or reset it.
- Several Z80s share the code. The running one's registers sit in a
  static struct, and `z80_run` copies the others in and out (24 words) as
  a slice starts. That costs less than reaching the registers through a
  pointer in every instruction.
- On the cart the core goes in `.time_critical.z80` (RAM). Build the file
  that includes it the way the plugins build their emulation:
  `-O3 -fno-data-sections`, with `Z80_NO_TRACE` defined for the cart and
  `Z80_TRACE` only `#ifndef Z80_NO_TRACE`.
- With several CPUs, call each one's handlers through a table indexed by
  `cpu` (as below). A `cpu ? a() : b()` puts two calls at every access
  site, which made Time Pilot's core 1.9 KB bigger.

### Time Pilot's two Z80s (the glue that passed its tests)

```c
#define Z80_CPUS 2
static const struct {
    uint8_t (*rd)(uint16_t);
    void (*wr)(uint16_t, uint8_t);
    uint8_t (*in)(uint16_t);
    void (*out)(uint16_t, uint8_t);
    uint8_t (*irq_ack)(void);
} tp_hooks[2] = {
    {tpm_hook_rd, tpm_hook_wr, tpm_hook_in, tpm_hook_out, tpm_hook_irq_ack},
    {tps_hook_rd, tps_hook_wr, tps_hook_in, tps_hook_out, tps_hook_irq_ack},
};
#define Z80_RD(cpu, a) tp_hooks[cpu].rd(a)
#define Z80_WR(cpu, a, d) tp_hooks[cpu].wr(a, d)
#define Z80_IN(cpu, port) tp_hooks[cpu].in(port)
#define Z80_OUT(cpu, port, d) tp_hooks[cpu].out(port, d)
#define Z80_IRQ_VECTOR(cpu) tp_hooks[cpu].irq_ack()
#ifndef Z80_NO_TRACE
#define Z80_TRACE(cpu) do { void (*t_)(void) = (cpu) ? tps_trace : tpm_trace; if (t_) t_(); } while (0)
#endif
#include "z80/z80.c"
```

### Star Trek's Z80 on the Sega G80 bus

`segag80v.cpp` puts taps on the program and opcode spaces that each take
2 cycles. The opcode handler reads the program space, so a fetch takes
4 extra and any other access 2. The USB's RAM at 0xd000-0xdfff is
installed after the taps and has no program-space wait. The board also
scrambles the write of each `LD (nn),A`.

```c
#define Z80_CPUS 1
#define G80_WAIT(a) ((((a) & 0xf000) == 0xd000) ? 0 : 2)
#define Z80_M1_WAIT(cpu, a) (2 + G80_WAIT(a))
#define Z80_RD_WAIT(cpu, a) G80_WAIT(a)
#define Z80_WR_WAIT(cpu, a) G80_WAIT(a)
#define Z80_OPCODE(cpu, a, op) (z80_scr_pc = (op) == 0x32 ? (a) : 0xffff)
#define Z80_WR_PAGED(cpu, a) (z80_scr_pc == 0xffff)
#define Z80_RD(cpu, a) stz_hook_rd(a)
#define Z80_WR(cpu, a, d) stz_hook_wr(a, d)
#define Z80_IN(cpu, port) stz_hook_in(port)
#define Z80_OUT(cpu, port, d) stz_hook_out(port, d)
#define Z80_IRQACK(cpu) stz_hook_irqack()     // INTCL; the vector stays 0xff
#include "z80/z80.c"
```

## Regenerating z80_ops.h

`z80_ops.h` is generated. Don't edit it by hand. Get `z80.lst` from MAME's
`mame0288` tag
(`https://raw.githubusercontent.com/mamedev/mame/mame0288/src/devices/cpu/z80/z80.lst`),
check it, and run the generator from this folder:

```sh
shasum -a 256 z80.lst   # b2050f64863dd611b3664bd91e984b7b1dc127c9e8a8ae4d4254643cac949eec
python3 -I z80gen.py z80.lst z80_ops.h
```

The output must match the checked-in file byte for byte. A newer MAME
`z80.lst` needs a look at `z80gen.py`'s replacements first.

## The smoke test

From the kit's top folder:

```sh
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-missing-field-initializers lib/z80/test.c -o z80test && ./z80test
```

It runs hand-assembled code on two Z80s and checks each instruction's
address and start cycle and the registers. It covers handlers that take
cycles or end the slice, IM 1 and NMI timing, the IRQACK-then-vector
order, M1 and memory waits, `Z80_OPCODE` and `Z80_WR_PAGED`, one CPU
resetting the other mid-run, and a CPU run in slices of 1 to 5 cycles
giving the same trace as one long slice. It prints `lib/z80: ok` and
exits 0, or lists what failed and exits 1.

## Cost on the cart

Measured with Time Pilot's and Star Trek's `armcount.sh`: the plugin built
for the Cortex-M33 with its own flags and run on Unicorn, 1200 lists of
play, at 150 MHz.

| Plugin | Plugin's own core | On lib/z80 |
|---|---|---|
| Time Pilot (two Z80s) | 1,818,589 cycles a list: 12.1 ms of 16.77, 99th percentile 15.0 ms | 1,825,505: 12.2 ms, 15.1 ms |
| Star Trek (a Z80 and two 8035s) | 1,820,324: 12.1 ms, 14.0 ms | 1,818,320: 12.1 ms, 14.0 ms |

The Z80's own time (`z80i_execute`) is a little lower than in the
plugins' cores: 889,776 cycles a list against 893,403 in Time Pilot.

The core is about 112 KB of code at -O3, in RAM on the cart. Time Pilot's
own was 110.8 KB, and the extra comes from the synced acknowledge and the
handler table.

## How it was proven

Both plugins passed their own MAME comparisons when built on this folder.
The tests ran in scratch copies outside the plugin repos and the kit,
with MAME headless (the plugins' `mamerun.sh` and `trace.sh` pass
`-video none -videodriver dummy -window -nomaximize -sound none
-nothrottle -skip_gameinfo -seconds_to_run`, with `SDL_VIDEODRIVER=dummy`).

```sh
export TMPDIR=/tmp/lib-proof KIT=<the kit> TP_ROMS=<your timeplt.zip> ST_ROMS=<your startrek.zip>
rsync -a --exclude 'build*' --exclude .git <parrot-plugin-timeplt>/ $TMPDIR/timeplt/
cd $TMPDIR/timeplt
rm src/z80.c src/z80.h src/z80_ops.h          # tpz80.c becomes the glue above; tpz80.h maps its names to z80_*
sed -i.bak 's|-I"$src"|-I"$src" -I"$KIT/lib"|' test/tp/*.sh
sh test/tp/compare.sh attract
sh test/tp/compare.sh game
sh test/tp/trace.sh 4
```

Star Trek works the same way: `src/stz80.c` becomes the glue above, and
`st.c`'s `stz_hook_irq_ack` becomes `stz_hook_irqack` (INTCL only), then
`test/st/compare.sh attract`, `compare.sh game` and `trace.sh main 4`.

| Plugin | Test | Compared | Result |
|---|---|---|---|
| Time Pilot | `compare.sh attract` | 3600 frames: both CPUs' registers, colour, video, sprite and work RAM, the sound CPU's RAM; 60 pictures | all identical |
| Time Pilot | `compare.sh game` | 7800 frames of play; 130 pictures | all identical |
| Time Pilot | `trace.sh 4` | 1,070,575 main and 710,774 sound CPU instructions, registers and cycle counts | all the same |
| Star Trek | `compare.sh attract` | 4000 frames: RAM, vector RAM, all three CPUs' registers and RAM | all identical |
| Star Trek | `compare.sh game` | 8000 frames of play | all identical |
| Star Trek | `trace.sh main 4` | 898,999 instructions from cycle 0 | all the same |

The Donkey Kong Jr. plugin has its own core, which runs whole
instructions and has stall and after-instruction hooks. Its board timing
is built around that, so moving it here means reworking that timing, and
it hasn't been tried.
