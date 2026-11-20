# Porting an emulator or other big code

The published plugins are worked examples, each with its full source:

| Plugin | Repository | Core | What to look at |
|---|---|---|---|
| NES | `parrot-plugin-nes` | QuickNES (LGPL-2.1) | `src/nescv.c`, the picture encoder the others reuse, and `nesapu.h` for sound |
| Game Boy | `parrot-plugin-gameboy` | Peanut-GB (MIT) | saves written in `cvf_on_exit()`, a speed-up that skips the game's wait loops |
| Master System, Game Gear | `parrot-plugin-sms-gamegear` | SMS Plus (GPL) | another machine on `nescv` |
| PC Engine | `parrot-plugin-pcengine` | PCE-GO (GPL-2.0) | a picture bigger than the TV: view keys and squeezing |
| Vectrex | `parrot-plugin-vectrex` | its own, following MAME's Vectrex driver (BSD-3-Clause) | vectors drawn into a bitmap |
| SCUMM | `parrot-plugin-scumm` | ScummVM's SCUMM engine (GPL) | a 900 KB plugin run from flash (`XIP`), a mouse pointer as a sprite, MIDI out to a computer |

They are at `https://github.com/plasticbugs/<repository>`. Read the one
closest to the job before designing anything.

## Plan around the budgets

| Budget | Size |
|---|---|
| CPU | one Cortex-M33 at 150 MHz for the plugin (core 1 serves the bus) |
| a TV frame | 16.7 ms |
| video RAM writes | about 1,800 bytes a frame, in the frame kernel |
| SRAM | about 507 KB for code, data and heap |
| PSRAM | 8 MB, slower than SRAM and read through a cache |

- **Keep the game's speed and the picture's rate apart.** Run the core at
  its own speed, 60 frames of game time a second, and send the TV a new
  picture only when the last one has fully arrived. A busy picture then
  takes a few frames, the game doesn't slow down, and the console always
  shows a whole picture. The NES and PC Engine plugins work this way,
  and the PC Engine plugin's note shows the pictures a second it got.
- **Don't change the system clock.** The SDK's bus loop on core 1 is
  timed for 150 MHz. A 200 MHz build of the PC Engine plugin crashed the
  console.
- **Big data goes in PSRAM**: the game's ROM, buffers, tables. Hot loops
  and the data they touch every cycle stay in SRAM. A cache miss to PSRAM
  costs far more than an SRAM read.
- **Measure on the cart.** The simulator runs the loop far faster than the
  M33 does. Put the slowest frame time, pictures a second and frames
  missed into the note (`cvf_note`), so the user can read them after
  RESET and tell you.

## The picture

The TMS9918 has 15 fixed colours, two per 8x1 run, and 256x192 pixels.
Most machines have more colours, finer colour placement and a taller
picture.

- **Colours**: map the machine's palette to the nearest of the 15 (the
  encoders use a table), then pick two colours per 8x1 run. Dark colours
  mostly vanish, because the palette has almost none.
- **Tiles**: if the machine draws from 8x8 tiles, as the NES, SMS and PC
  Engine do, cut the picture along its tile grid. Most cells repeat, so
  each third of the screen keeps a cache of 256 cell patterns and a
  picture costs 16 bytes for each new cell plus the names that change.
  `nescv.c` does this with double-buffered name tables, so a picture is
  never shown half drawn.
- **Height**: games draw 224 to 288 lines and the TV shows 192. Scaling
  the whole picture by 3/4 often fits it with nothing cut off. Of each 4
  lines, keep the 1st and the 4th and turn the middle two into one. Where
  one of those two differs from the lines on both sides of it, keep that
  one, so a stroke 1 pixel wide survives. Do the same with the columns. A
  240x256 picture becomes 180x192, with black at the sides. If it's still
  too tall, scale only the play area and move the score and lives into
  the side margins. A scaled picture no longer lines up with 8x8 tiles,
  so send it as a bitmap: keep a copy of the console's pattern and colour
  tables and send the bytes that change. A view the user moves with
  keypad keys is the other way, for games where fine detail matters more
  than seeing the whole screen, such as ones full of text. Pick a
  sensible start for it.
- **Sprites**: the TMS9918's 32 sprites, 4 to a line, are useful for a
  mouse pointer or a few objects, not for another machine's 64.
- **Model video RAM.** Keep a copy of what the console holds, send only
  the bytes that differ, and keep each list until `cvf_publish()` takes it
  so the model stays in step.

## The sound

The SN76489 has three square-wave voices and a noise voice. Read the
machine's sound registers once a TV frame and turn them into PSG bytes:
the three most important tones, and noise for drums. Samples and voices
don't carry over. `nesapu.h` and the SMS plugin are examples. Send the
bytes in the frame list (command 2), and expect a sound write to cost the
Z80 about 29 cycles more than the chip's datasheet says.

## Memory and the build

- Emulator cores usually call `malloc`: add `HEAP`, call
  `cvheap_reserve()` first, and watch `cvheap_largest()`.
- Remove `printf` and file I/O from the core, or point them at your own
  functions. A stray `fprintf` pulls in about 19 KB of newlib.
- Build the core with `-O2` or `-O3`, and its vendored code with its
  warnings off. Keep the core in `vendor/` with a `VENDORED.md` that lists
  your changes.
- If it doesn't fit below `0x2007EC00`, try `-Os` for cold code first.
  `XIP` (run from flash) is the last step, and the code that talks to the
  console still has to be in RAM (`cvxip.h`).

## Testing on the Mac first

Compile the core and your encoder for the Mac against the kit's models
(`test/host/build.sh`, `sim/run.sh --src ... --include ...`). Then:

- play it in the simulator with a game the user owns, from a folder given
  as `--card`;
- look at `--shot` pictures against what the machine would show;
- add a block to `test/run.sh` so the encoder's lists are checked
  against the real kernel in MAME.

Never commit or attach a game, a BIOS or a ROM. Tests read them from the
user's own folders, and README files tell users where to put their games
on the card (`HOME`) and which files the plugin opens (`OPENS`).

## Licenses

The core's license usually decides the plugin's. GPL and LGPL code needs
the README to link to the source of the exact release (the tag), and
every core, font and table that isn't yours goes in the README's credits
and `THIRD-PARTY.md`, with its license.
