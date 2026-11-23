# lib/sp0250: the GI SP0250 speech chip

General Instrument's SP0250 LPC speech synthesizer, as MAME 0.288 makes
its samples: the same 15-byte FIFO and DRQ line, pitch and repeat counts,
15-bit noise generator, and six two-pole filters, sample for sample, at
the chip's own rate. That rate is clock / 312: 10,000 a second at the
usual 3.12 MHz. Sega's G80 speech board (Star Trek, Space Fury, Zektor)
and some of Gottlieb's sound boards use it.

- `sp0250.h`: the state (`sp0250_t`, one per chip) and the functions.
- `sp0250.c`: the code. Include it from one `.c` file; the others include
  `sp0250.h`.
- `test.c`: a smoke test.

MAME's PWM output mode (`set_pwm_mode`) isn't here. Sega's speech board
doesn't use it.

## Where it came from

It's a C port of MAME's `src/devices/sound/sp0250.cpp` (tag `mame0288`;
copyright holder Olivier Galibert; BSD-3-Clause), taken from the Parrot
Cart Star Trek plugin's `src/st.c` and given its own state.

## Using it

```c
#include "sp0250/sp0250.c"              // in one .c file

static sp0250_t sp;
sp0250_init(&sp);                       // power on and reset: DRQ asserted
sp0250_write(&sp, byte);                // the CPU's write: a byte into the FIFO
int drq = sp0250_drq(&sp);              // the DRQ pin, 1 while the FIFO has room
int8_t s = sp0250_next(&sp);            // the next sample, -64..63 (MAME's stream: s / 128.0)
sp0250_render(&sp, buf, n);             // n samples
```

| Function | What (MAME's name) |
|---|---|
| `sp0250_init(s)` | power on (`device_start`, the noise generator at `0x7fff`), then `sp0250_reset` |
| `sp0250_reset(s)` | `device_reset`: a frame taken from the FIFO as it is, and the FIFO emptied, DRQ on |
| `sp0250_write(s, d)` | `write`: a byte into the FIFO. DRQ goes off with the 15th; a 16th is dropped. |
| `sp0250_drq(s)` | the DRQ line: 1 (ASSERT) while the FIFO isn't full |
| `sp0250_next(s)` | one sample. When the frame's repeats are done, the next frame is taken if the FIFO is full (DRQ comes back on); if not, the chip carries on as it was, a repeat at a time. |
| `sp0250_render(s, out, n)` | `sp0250_next` n times |
| `SP0250_RATE(clock)` | the sample rate for a clock |
| `s->samples` | samples made since `sp0250_init` |

### Keeping time as MAME does

The FIFO and DRQ change only in `sp0250_write` and `sp0250_next`, so
the board has to make the samples up to "now" at the moments MAME does,
or the CPU that feeds the chip sees DRQ at the wrong time. MAME brings the
chip's stream up to date before each write and on a timer at the chip's
rate (its DRQ timer). Bringing it up to date at time t makes every sample
whose index is at most floor(t x rate) (none at time 0):

```c
// before each write, and on a timer every 1/rate s (with lib/sched: sched_now(), sched_ticks)
static void sp_update(void) {
    if (sched_now() == 0 && sched_epoch() == 0) return;
    uint64_t upto = sched_ticks(sched_now(), SP0250_RATE(3120000));
    while (sp.samples <= upto) out_sample(sp.samples, sp0250_next(&sp));
}
void speech_write(uint8_t d) { sp_update(); sp0250_write(&sp, d); }
```

The board's CPU usually reads DRQ on a test pin (the Sega speech board's
8035 polls it on T1), so `MCS48_T1()` can return `sp0250_drq(&sp)`.

## How fast

A sample is six filters, a dozen multiplies and adds. In the Star Trek
plugin each one costs the cart about 290 M33 cycles with the update
around it, so 10,000 a second take 2.9 million cycles, 2% of its 150 MHz
(the plugin's `test/st/armcount.sh`, Unicorn with the M33's book cycle
times, over 600 lists). The plugin's own copy cost the same.

## The smoke test

From the kit's top folder:

```sh
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-missing-field-initializers -Ilib lib/sp0250/test.c -o /tmp/sp0250test && /tmp/sp0250test
```

It feeds two made-up frames, a voiced one and then noise, and checks the
samples against the ones the Star Trek plugin's copy makes (the first 24
and a hash of 3,000). It also checks the DRQ line, the FIFO's overflow, the
idle repeats, the rate, and that `render` is `next` n times. It prints
`sp0250: all passed`, or what failed and exits 1. It also builds without
warnings for the cart (`arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -O2
-Wall -Wextra -std=c11 -Ilib -c lib/sp0250/test.c`).

## How it was proven

- **Against the Star Trek plugin's copy**: random FIFO writes over 2,000
  runs of 4,000 samples, 8 million samples in all. The samples and DRQ
  were the same at every one.
- **In the Star Trek plugin** (`ST_ROMS`: MAME's `startrek.zip`), built
  against `lib/` (copies of the plugin, outside its repository), with its
  SP0250 replaced by this one and its 8035s by `lib/mcs48`:
  - `test/st/compare.sh attract` and `game` matched MAME 0.288 at every
    frame (4,000 and 8,000), the speech board's 8035 included. That CPU
    waits on the SP0250's DRQ, so this checks DRQ's timing.
  - The 2,000,001 SP0250 samples of the 8,000-frame game (st_host
    `-sndlog`) were byte for byte the plugin's own.
  - `test/st/soundcmp.sh` (with `MAME_ROMPATH` for the ColecoVision BIOS)
    gave the plugin's figures against MAME's recording: -17.9 against
    -17.7 dBFS rms, a log-spectral distance of 5.3 dB, and -22.5 dBFS rms
    replayed on MAME's ColecoVision.
