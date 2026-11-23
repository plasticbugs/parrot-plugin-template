# Porting an emulator or other big code

The published plugins are worked examples, each with its full source:

| Plugin | Repository | Core | What to look at |
|---|---|---|---|
| NES | `parrot-plugin-nes` | QuickNES (LGPL-2.1) | `src/nescv.c`, the picture encoder the others reuse, and `nesapu.h` for sound |
| Game Boy | `parrot-plugin-gameboy` | Peanut-GB (MIT) | saves written in `cvf_on_exit()`, a speed-up that skips the game's wait loops |
| Master System, Game Gear | `parrot-plugin-sms-gamegear` | SMS Plus (GPL) | another machine on `nescv` |
| PC Engine | `parrot-plugin-pcengine` | PCE-GO (GPL-2.0) | a picture bigger than the TV: view keys and squeezing |
| Vectrex | `parrot-plugin-vectrex` | its own, following MAME's Vectrex driver (BSD-3-Clause) | vectors drawn into a bitmap |
| Star Wars, The Empire Strikes Back | `parrot-plugin-starwars`, `parrot-plugin-esb` | their own boards, following MAME's Atari Star Wars driver (BSD-3-Clause) | an arcade vector game drawn into a bitmap, the TMS5220's speech through the DAC, a flight yoke on the stick |
| Donkey Kong, Time Pilot, Mr. Do! and the other arcade games | `parrot-plugin-dkong`, `parrot-plugin-timeplt`, `parrot-plugin-mrdo`, ... | their own boards, following MAME's drivers (BSD-3-Clause) | a portrait screen at 3/4 with the HUD in the margins, the sound CPU, tests against MAME (Time Pilot's and Mr. Do!'s `test/`) |
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

## Timing

`parrotcart_frames()` builds the plugin with the frame kernel and the
SDK's two-sample bus loop (`cvframes_serve2()`, `cvfserve.S`). Core 1
acts on an address only when two samples of the console's pins, 60 ns
apart, agree. The older loop acted on one sample. On a console, the
74HCT245 switching the data bus spoils a sample now and then, and with
the old loop The Empire Strikes Back got wrong bytes: commands out of
place, a green border, a restart. With the new loop it played clean, and
all the cart's frame plugins are built with it now. Don't switch it off,
and leave the clock at 150 MHz, which it is timed for.

- **The budget.** A frame is 59,736 Z80 cycles, and the vertical blank
  is the first 15,960 of them. Add up each list's cost as you build it,
  by the costs in `gbkernel.asm`'s header: 480 for the kernel's start
  (660 with `CONTROLLER2`), a video RAM run 106 plus 23 a byte in the
  blank and 31 a byte after it, sound 75 plus 52 a byte (the chip holds
  the Z80 for about 29 cycles at each byte), a register 114, and 54 and
  52 for the two ends. The models in `test/run.sh` and the simulator
  fail a list whose blank part runs past 15,360 cycles or whose whole
  runs past 58,536. The template keeps 2,000 and 4,000 cycles in hand.
  That leaves room for about 1,800 bytes of video RAM a frame.
- **Safe to run twice.** When no new list has come, the console runs the
  last one again. Absolute video RAM writes, registers and sound bytes do
  no harm twice. Writing the noise's kind and rate restarts the noise, so
  write that register only when it changes.
- **Publishing.** `cvf_publish()` copies the list and returns false while
  the one before is still waiting. If you keep a copy of the console's
  video RAM, never drop a list. Keep it and offer it again until it's
  taken, or your copy and the console part ways. Make the next list as
  soon as the last is taken. A list that isn't there in time means the
  console runs the last one again, and its sound repeats.
- **Telling you're behind.** The SDK has no signal for it, but
  `cvf_frames()` counts every list the console starts, a repeated one
  too. Count the lists you publish. When `cvf_frames()` has gone up by
  more than that since the start, the difference is the lists the
  console ran again. The arcade plugins put it in their note.
- **The note.** The cart's arcade plugins leave one like
  `60/s 0L 8.7ms s0 h1`: pictures a second, lists the console ran again
  since the start, the slowest list lately, and from `cvf_bus_counts()`
  the addresses the bus loop refused (`s` looked like a select, `h` like
  the header). A few refusals are normal. They are addresses the old loop
  would have acted on.

### Lists back to back

A plugin that plays samples (below, "The sound") makes every list a
little longer than a frame. The console then goes from one list straight
into the next without waiting for the frame's start, and a list's length
is the sample clock. Build it with `parrotcart_frames(mygame
BACK_TO_BACK)`. That changes nothing on the cart, but the models then
check each list past 58,536 cycles as a back-to-back one:

- By MAME's costs (below), the list plus the kernel's 459 cycles between
  lists must come to more than a frame, 59,736. A shorter one makes the
  console wait for the next frame, a gap in the sound, and the models
  fail it. So does the MAME step in `test/run.sh`, which counts the
  lists followed by a wait.
- Only the first list starts in the vertical blank. The others start
  wherever the last one ended, so their blank part must be empty and
  every video write goes in the second part, at 31 cycles a byte.
- `cvf_frames()` counts the lists the console starts, not TV frames.

`examples/samples` does all of it: 100 samples a list, evenly spaced,
with runs of the picture between them to fill the time. The cart's
arcade plugins make each list 60,036 cycles by the header's costs, 300
over a frame, which is 5,962 samples a second.

The models check a list by the header's costs. MAME 0.288 runs a sound
byte a cycle faster than the header counts, so size a back-to-back list
by the header and leave a hundred cycles or so to spare. Then it's long
enough by either count:

| | `gbkernel.asm`'s header | MAME 0.288 |
|---|---|---|
| a video RAM run of n bytes, after the blank | 106 + 31n | the same |
| sound, n bytes | 75 + 52n | 75 + 51n |
| from a list's end to the next one's first command, back to back | | 354 (533 with `CONTROLLER2`) |
| the NMI, once a frame, wherever the list is | | 105 |
| from the last sample's first write to the next list's first | | 688 |

A list of 100 samples has about 310 sound bytes, so by MAME's costs it's
about 0.5% shorter than the header says, and its samples play that much
faster in MAME. That's fine for the sound, but don't expect the header's
costs to give an exact sample clock.

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
  the whole picture by 3/4 often fits it with nothing cut off, and the
  SDK does it: `cvscale34()` in `cvscale.h`, built into every frame
  plugin. Of each 4 lines it keeps the 1st and the 4th and turns the
  middle two into one. Where one of those two differs from the lines on
  both sides of it, it keeps that one, so a stroke 1 pixel wide survives.
  Then it does the same with the columns. It works on pens (the machine's
  colour numbers, any 8-bit values), so scale first and map to the
  TMS9918's colours after. A 224x256 picture becomes 168x192, and a
  240x256 one 180x192, with black at the sides.
  For other sizes (278 columns to 256, 215 lines to 179), the kit's
  `lib/scalen` does the same with the pairs spread evenly, and can make
  one 8-line band at a time to save RAM.

  ```c
  // w x h pens (multiples of 4) to 3w/4 x 3h/4; columns cx0 to cx1 - 1 only (0, w: all)
  cvscale34(src, src_stride, w, h, 0, w, dst, dst_stride);
  ```

  A scaled picture no longer lines up with 8x8 tiles, so send it as a
  bitmap: keep a copy of the console's pattern and colour tables and send
  the bytes that change. A view the user moves with keypad keys is the
  other way, for games where fine detail matters more than seeing the
  whole screen, such as ones full of text. Pick a sensible start for it.
- **The arcade layout**: a portrait arcade screen 224 wide comes out 168
  wide at 3/4. Put it in the middle, at x 44, and five 8-pixel columns on
  each side are free, since none of them shares an 8x1 run with the game.
  The cart's Donkey Kong and Pac-Man plugins cut the score, high score,
  lives, level and bonus out of the status rows and set them in those
  margins, with the play area in the middle. If the play area is still
  too tall, scale only it, and move the rest to the margins the same way.
  Text in the margins reads better at the arcade's own size: five columns
  hold five of its 8x8 characters, so draw labels, scores and lives
  unscaled, stacked where they don't fit across, and scale only what has
  to shrink. Scaled 8x8 text loses strokes: Time Pilot's CREDIT came out
  CREDII at 3/4, its T's crossbar gone, until its HUD went to the
  arcade's size. Check every piece of the margins against the arcade's
  picture, glyph by glyph, for a lost stroke or a closed gap.
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

Write the whole chip in every list: each voice's tone and volume and the
noise volume, including what hasn't changed and the volumes of voices
you don't use. Now and then the console reads a byte from the cart
wrong, and if that byte was a sound write, the register stays wrong
until something writes it again. An arcade plugin that set its tones up
once, and never wrote the voices it didn't use, got a low buzz under the
game after one bad byte. It lasted until RESET. Ten bytes cost about 1%
of a frame. If you play samples through the volume registers, set the
tones' pitch again in every list as well. Leave the noise's kind and
rate until they change, because writing that register restarts the
noise.

Samples can go out through tones parked at a period of 1, with their
volumes summed as a DAC. Use three tones: with two, Donkey Kong's sound
had only about 20 distinct levels and was scratchy in quiet parts, and a
third tone fixed it. `tools/mkdac3.py tools/ladder.json OUT.h PREFIX`
makes the table: 512 levels, each a choice of the three attenuations,
from levels measured on a real console, chosen so neighbouring levels
differ little (the console hears a sample's three writes one at a time).
The cart's Q*bert, Pac-Man, Donkey Kong and Time Pilot plugins play their
sound that way, about 6,000 samples a second, with their lists back to
back (above, "Lists back to back"). `examples/samples` is the small
version to start from. It's more work than tones, so try tones first.

### How loud

The cart's first arcade plugins came out far too quiet next to a
cartridge, so the TV had to be turned well up for them. Aim for the
loudness of an ordinary ColecoVision cartridge at the same TV setting.

Four cartridges recorded in MAME with `-wavwrite` and measured with
`tools/wavlevel.py` give an `rms` of -20.5 to -23.9 dBFS (Donkey Kong
-20.5, Smurf -21.1, Lady Bug -23.9, Mr. Do! -23.9), about -22 on
average. Aim within 3 or 4 dB of that and not above it. The first
arcade plugins measured about 12 dB below it.

- With tones, map the machine's full volume to the chip's full volume
  (attenuation 0), not to the middle of its range, and keep the balance
  between the voices.
- With samples, scale the mix up to that level, and round the peaks off
  with a soft knee instead of clipping them. Below the knee k (about 3/4
  of full scale) a sample stays as it is. Above it, with x and y as
  fractions of full scale and the sign kept:
  `y = k + (1 - k) * (1 - exp(-(x - k) / (1 - k)))`. Put it in a table.
- Measure it. Record a cartridge you own in MAME with `-wavwrite`, and
  the plugin's lists with `MAME_WAV=plugin.wav tools/mame-replay.sh`, then
  compare them with `python3 tools/wavlevel.py cart.wav plugin.wav`
  (`docs/testing.md`, "How loud"). Then ask the user to listen on a TV.
- The MAME step needs the ColecoVision BIOS, `coleco.zip`, which the kit
  can't include. Ask the user for the folder that has it and set
  `MAME_ROMPATH` to it. Never copy it into the repository. Without it,
  `python3 tools/listlevel.py lists.bin` works the level out from the
  lists in the same dBFS, within about half a dB of MAME.
- To compare with the arcade, `tools/wavlevel.py --span arcade.wav
  plugin.wav` measures over the arcade's seconds with sound, and
  `tools/knee.py` says what the knee takes off each part of the mix.

## Arcade games

Every arcade plugin uses the same keys, so someone who knows one knows
them all:

| | ColecoVision controller | USB pad |
|---|---|---|
| move | the stick | the d-pad or the left stick |
| fire (or jump) | either fire button | any face button |
| twin-stick fire (Robotron) | controller 2's stick | the right stick, or the face buttons (two at once for a diagonal) |
| coin | keypad * or # | - (minus; Share on a PS4 pad) |
| one-player start | keypad 1 | + (plus; Options on a PS4 pad) |
| two-player start | keypad 2 | |
| back to the menu | RESET | |

If the game also starts on a fire button, keep that. A one-player game
leaves keypad 2 unused. A game with two fire buttons can give each one
its own: the left fire and `GP_SOUTH`, the right fire and `GP_EAST`. Make
a coin a pulse of about 6 frames from the press, however long the key is
held, and need the key up for 3 frames before the next one. Some games
ignore a coin switch held shut for long, and a key that bounces should
still give one coin.

Read the pad yourself, as below, and not through `cvin_update()`. That
one presses * and # with the pad's top and left buttons, which in an
arcade game would be coins.

A game with a dial or spinner (Star Trek, Tempest) takes it from
`lib/dial/dial.h`: `dial_update(&dial, left, right)` once a list gives
the dial's move from the stick held left or right (faster the longer
it's held), a pad's left stick in proportion, and a mouse's x.

A twin-stick game moves with controller 1's stick and fires the way
controller 2's stick points, in 8 directions. Build it with
`parrotcart_frames(mygame CONTROLLER2)` so the kernel reads controller 2.
On a USB pad, the left stick moves and the right stick fires. A pad
whose right stick doesn't read (a DualShock 4, `docs/features.md`) fires
with the face buttons: top up, bottom down, left left, right right, and
two at once for a diagonal. Keypad 1 and 2, * and # do what they do in
every arcade game, on controller 1.

```c
// once a list, for player 1: the console's controller and USB pad 1
uint8_t joy, fire, lines;
cvf_input(&joy, &fire, &lines);                     // low = pressed
const int key = cvin_decode_key(lines);             // cvinput.h: 0-9, CVIN_STAR, CVIN_HASH, -1
uint32_t dirs = 0, buttons = 0;
gamepad_t p;
if (cvusb_pad(0, &p)) { dirs = cvusb_pad_dirs(&p); buttons = p.buttons; }
const bool up = !(joy & 1) || (dirs & GP_UP);      // right 2, down 4, left 8, the same way
const bool shoot = (fire & 3) != 3 || (buttons & (GP_SOUTH | GP_EAST | GP_WEST | GP_NORTH));
const bool coin = key == CVIN_STAR || key == CVIN_HASH || (buttons & GP_MINUS);
const bool start1 = key == 1 || (buttons & GP_PLUS);
const bool start2 = key == 2;
static int coin_left, coin_wait;                    // one coin a press
if (coin && !coin_wait) coin_left = 6;
if (coin) coin_wait = 3; else if (coin_wait) coin_wait--;
const bool coin_switch = coin_left > 0;             // the game's coin input for this list
if (coin_left) coin_left--;
```

For a twin-stick game, the fire direction from controller 2 and the pad:

```c
// once a list, after the code above; needs parrotcart_frames(mygame CONTROLLER2)
uint8_t joy2, fire2, lines2;
cvf_input2(&joy2, &fire2, &lines2);                 // controller 2, low = pressed
int16_t rx = 0, ry = 0;
if (cvusb_pad(0, &p)) { rx = p.rx; ry = p.ry; }     // the right stick, y up
const bool fire_up    = !(joy2 & 1) || ry > 820 || (buttons & GP_NORTH);
const bool fire_right = !(joy2 & 2) || rx > 820 || (buttons & GP_EAST);
const bool fire_down  = !(joy2 & 4) || ry < -820 || (buttons & GP_SOUTH);
const bool fire_left  = !(joy2 & 8) || rx < -820 || (buttons & GP_WEST);
// (up and down together, or left and right, cancel: no fire that way)
```

`examples/two-controllers` shows both controllers and the pad's two
sticks live, with this rule.

Arcade controls the console doesn't have go on what it does have, and
the README says how:

- **A spinner or dial** goes on the stick: hold left or right to turn,
  faster the longer it's held, up to about the arcade's top speed. On a
  USB pad, the left stick's x turns in proportion.
- **A yoke or analog stick** follows the stick: it moves toward the end
  the stick points to and springs back to the middle when it's let go,
  as in Star Wars. On a USB pad, the left stick sets it directly.
- **More buttons than two**: the two most used get the fire buttons
  (`GP_SOUTH` and `GP_EAST` on a pad), the next go on the stick's up and
  down if the game doesn't use them, and the rest on keypad keys other
  than 1, 2, * and # (and on `GP_WEST`, `GP_NORTH`, `GP_L` and `GP_R`).

The pads need nothing more from the plugin. The SDK finds a pad that's
already on a powered hub when the plugin starts, and one plugged in or
pulled out while it runs (it reads the ports of hubs that never report a
change). On a console that's been tried with the Switch Pro Controller, a
PS4 controller, 8BitDo pads in their Switch mode, and an Anker hub with a
Genesys GL3523 chip. A game whose stick has four ways, not eight, should
turn a diagonal into one of them the way MAME does: the way that came on
last wins.

### The ROM set

The user's own MAME set goes in the plugin's `HOME` folder on the card,
as the zip MAME uses (`/DKONG/dkong.zip`) or as its loose files. The SDK
reads the zip: `swzip.h` lists its entries and inflates them (add
`${PARROTCART_SDK}/sdk/swzip.c` to the sources). Find each file by its
CRC from MAME's driver, not by its name, since sets name them
differently, and say on the screen which one is missing or wrong. Decode
the PROMs (palette, colour lookup) when the plugin starts. Never commit a
ROM; tests read the user's set from a folder they name.

### High scores and settings

A board with battery-backed RAM (CMOS, NVRAM) keeps its high scores and
settings there. Keep it on the card in the plugin's `HOME`, in the same
format as MAME's `nvram/<set>/` file, so a user can copy theirs across
either way. Robotron keeps MAME's `nvram` file as `/ROBOTRON/NVRAM`.

- Make the file once, at its full size, at the start (`fs_create()`),
  so later writes go to sectors that are already there.
- Check what you read. A file that's all zeros, or not in the board's
  format, means a fresh start, as MAME does with no file.
- Write it during play, a sector a list, and only when it has changed
  and the game is between waves or at its high-score table. A sector
  write holds the loop up for a few milliseconds, and one a list doesn't
  make a list late. Writing a whole file at once would.

### The arcade picture

- **Palettes that change down the screen.** Some boards change pens as
  the beam draws (Robotron changes its 16 pens line by line). Take the
  board's picture a line at a time as the beam reaches it, with that
  line's palette, and note which 8-pixel words and pens changed so
  the encoder only redoes those.
- **Not 60 pictures a second.** Star Trek draws 40 pictures a second.
  Run the game at its own rate and, at each of its frames, make its
  picture the next one only if the console has had all of the last one.
  Otherwise skip that frame's picture and let the game run on. It shows
  about 36 a second that way, and the sound, which is in every list,
  never waits for the picture.
- **Vector games.** Draw each vector as a line one pixel wide into a
  bitmap the size of the TV. On two colours per 8 pixels, the colour
  drawn last wins. Give way where it matters, as Star Trek's stars' grey
  gives way to any other colour. Check the nearest-colour map by eye:
  the nearest TMS9918 colour in OKLab can be the wrong hue (Star Trek's
  dim olive came out dark red, so it's dark yellow). Small symbols that
  become specks at the TV's size can be drawn bigger about their own
  centre, in the same place. To do that only on the right screen, find
  a vector that only that screen draws (Star Trek's scanner edge).

### Match MAME exactly

Build the board (its CPUs, memory map, inputs and video and sound
registers) as code that compiles both for the cart and for the Mac, and
test it on the Mac against MAME's driver for the game, run headless.

Start from the kit's `lib/` (`lib/README.md`): the Z80, 8035, 6809 and
6800 as MAME 0.288 runs them, the SP0250 speech chip, and MAME's
scheduler for a board with several CPUs and timers. Each was checked
against MAME instruction by instruction inside a real plugin, so a
mismatch is in your board, not the CPU. They run one CPU or several from
one copy of the code (one Z80 core for both of Time Pilot's Z80s fits in
RAM once). The board's `.c` file sets each CPU's memory map and ports as
macros and includes the core, so its reads and writes are inlined.


- **Trace**: MAME's debugger, headless (`-debug -debugger none` with a
  `-debugscript` that turns on `trace` for each CPU), logs every
  instruction with its registers. Write the same lines from the model and
  compare them from the second frame on, for a couple of seconds of the
  attract mode. The first different line is the bug.
- **Compare**: a Lua script (`-autoboot_script`) applies a script of
  inputs at each VBLANK and dumps the board's state: video, colour, work
  and sprite RAM, each CPU's registers, the sound CPU's RAM. The model
  dumps the same, and they must match at every frame: a minute of
  attract mode, then a played game, where the model's own bot plays,
  its inputs go into the script, and MAME plays the same script. MAME runs
  an autoboot script again on a soft reset, so guard its setup with a
  global (`if not _G.done then _G.done = true ... end`).
- **Pictures**: every 60th frame, MAME's `screen:pixels()` beside the
  model's picture before scaling, and the console's picture from
  `tools/mame-replay.sh`.

The Time Pilot and Mr. Do! plugins (under "Where to look") have these
tests in `test/`: `trace.sh`, `compare.sh` with its Lua dump script,
`hudcheck.sh` for the margins and `armcount.sh` for the cost below.
Donkey Kong Jr., Star Trek and Robotron have them too, for boards with
an 8035, three CPUs, and a 6809 with blitters.

Details in MAME's driver that decide whether the model matches:

- **The ROM layout.** `ROM_LOAD` with `ROM_CONTINUE` puts the pieces of
  one file at several addresses. Donkey Kong Jr.'s program is three files
  in ten pieces. Copy the driver's table exactly.
- **The palette.** Many boards drive resistor networks from PROM bits,
  sometimes inverted by the monitor. Take the levels from MAME's palette
  function, not from a guess at the resistors, and check a few colours
  against MAME's `screen:pixels()`.
- **When the CPU is held.** A DMA that stops the CPU (Donkey Kong Jr.'s
  8257 copying the sprites) lets it go at the end of one of MAME's
  scheduler slices, not when the copy ends. The game's timing depends on
  it. Model MAME's rule, and let the Mac tests replay MAME's own holds.
- **The CPU clock.** MAME rounds a divided clock. Robotron's 6808 runs at
  3,579,545 / 4 rounded up, which moves a sound command by an
  instruction over a game.
- **Instructions that delay interrupts.** After some instructions (the
  6800's TAP, a CLI that clears the mask) MAME runs the next instruction
  before taking an interrupt. Do the same.

MAME's own tools have pitfalls:

| What | What to do |
|---|---|
| The debugger's `tracelog` traces only the CPU the debugger shows, and starting the debugger shows the main CPU | trace the other CPUs from the second frame on, or start a run per CPU |
| `tracelog` prints nothing before the first frame | compare from frame 2 |
| The MCS-48's Lua state has no PSW | read it from the debugger's trace, or leave it out of the dump |
| Lua's `frame_done` runs before the inputs are updated | key the input script to machine time, not to frame callbacks |
| Lua has no `read_range` | read memory a byte at a time with `read_u8` |
| MAME stops in the middle of an instruction at a frame's end, so two dumps can differ for one frame | fail a compare only when a difference lasts two frames or more |
| Some coin circuits ignore the first coin after power-on | tap the coin once at the start of the script, as the Star Trek tests do |

To look at pictures, `tools/png256.py side OUT.png mame.png plugin.png`
puts two side by side with a third panel marking where they differ,
`sheet` makes a contact sheet of many, and `halve` takes a 2x snapshot
back to 1x. `lib/tms9918_rgb.h` has the console's colours for previews a
test writes itself.


### The sound board

Run the sound CPU in the model too (Donkey Kong's 8035, a Konami board's
Z80 and AY chips), and mix what it makes into one sample stream at the
lists' rate for the three-tone DAC. Sounds made by circuits instead of a
chip, as Donkey Kong's walk, jump and stomp are, can be synthesized from
MAME's discrete model of the circuit (MAME's sound code is BSD-3; credit
it). Record the game in MAME with `-wavwrite` and the plugin with
`MAME_WAV`, and compare their loudness with `tools/wavlevel.py` and their
shape by ear and by lining the two up.

MAME often runs a synthesizer circuit far faster than the cart can.
Star Trek's sound board runs at 2 MHz in MAME. The plugin runs it at two
steps a sample, about 11,900 a second, and stays within 0.2 dB of MAME's
level:

- integrate each square wave exactly over the step (the part of the step
  it's high), instead of sampling it
- average a noise generator's bit over the step
- convert each filter to the step's rate (the same cutoff, recomputed
  for the new step)
- where a filter is switched in and out, weight it by how long it's in
  during the step

A speech chip that makes its own samples (an SP0250, a TMS5220) can run
exactly, and each of the cart's samples is the mean of the chip's
samples it covers.

### Is it fast enough

A list is 16.77 ms of the console's time, 2.52 million cycles at 150 MHz,
for the board, its sound and the picture. The simulator can't say: it
runs far faster than the M33. `tools/armcount/` can: put a list's work in
a copy of its `bench.c`, build it with `build.sh` and the plugin's flags,
and `armcount.py` runs it in Unicorn and charges the M33's book times per
instruction (`docs/testing.md`, "Fast enough on the cart"). Time Pilot averages 12.8 ms a list this way, worst
15.5, and Donkey Kong about 14. Keep one CPU core for both
of a board's Z80s (`lib/z80` does), so it fits in RAM once. If a busy picture can't keep
up, send part of it now and the rest next list (Donkey Kong caps a
picture at 96 changed tiles) rather than slow the game. Count late lists
in the plugin's note (`cvf_note`), since only the cart has the last word.

The picture is usually the biggest cost, more than the CPUs:

- Redraw only what changed: the tiles that changed and the blocks under
  sprites, the bands of a scaled picture with a changed line in them, the
  8-pixel runs whose pixels changed.
- Give the picture a budget a list. Robotron spends about 0.8 million
  cycles on it, and a picture that needs more (the whole screen changes
  between waves) carries on at the next list from that frame's lines. It
  comes a few frames late, and no list is late.
- On the M33 a 64-bit divide costs about 100 cycles, and newlib's
  `memcmp` and `memset` are slow for short runs. Keep them out of hot
  loops.
- A delay loop in the game's code (Robotron's sound ROM spends a third
  of its time in `DECA` / `BNE`) can run all but its last turn at once,
  which leaves the board as running each instruction would. Check that
  against the full run.
- A hook for the Mac tests must not change the board's state. One in
  Donkey Kong Jr. hid a bug that blanked the score.

## Memory and the build

An arcade plugin runs out of RAM before it runs out of time. All of it,
code included, runs from the 507 KB of SRAM, and `.bss` must end below
`0x2007EC00` (the build says so when it doesn't).

| What | Size |
|---|---|
| the frame kernel's buffers in the SDK | about 64 KB |
| the screen kernel's buffers in the SDK, and a `cvs_t` (its canvas is about 49 KB) | about 64 KB and 66 KB |
| core 0's stack | 2 KB |
| a copy of the console's pattern and colour tables | 12 KB |

- The stack is small, so a big array is `static`, never a local.
- Pack tables. Donkey Kong Jr. keeps its tiles' rotations in 16 KB
  instead of 64, and its copy of the console's screen in 24 KB instead
  of 48, by scaling a band at a time.
- Build the emulator's files with `-O3 -fno-data-sections`, set per file
  in `CMakeLists.txt` (`set_source_files_properties`), the rest with
  `-O2`, and the card and start-up code with `-Os`. The SDK's
  `-fdata-sections` puts each variable in a section of its own, so GCC
  loads each one's address separately. Without it, GCC reaches all of a
  file's variables from one base address, which is faster in a hot loop.
- With GCC 9 a global without `static` and without a value is a COMMON
  symbol. A linker script of your own (for a cycle counter, say) needs
  `*(COMMON)` in its `.bss`.
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
