# Testing

## On your computer

```sh
test/run.sh                    # the template's plugin and every example
test/run.sh hello sound        # some of them
NO_MAME=1 test/run.sh          # without the MAME step
```

`test/run.sh` compiles each plugin's `main.c` for your computer and runs
it against models of everything on the cart (`test/host/host.c`):

| The model | What it does | What it catches |
|---|---|---|
| the screen kernel | runs each list a frame after it's sent, sends HELLO and ACK reports, and reports the controller from a script | a malformed list, a list sent too early, three colors in an 8-pixel run, and a screen where the SDK and the console don't match |
| the frame kernel | runs one list each TV frame and adds up its time from the kernel's costs | a list that runs past its frame or its vertical blank, and bad commands |
| the card | a disk image, read through the SDK's file system (FAT16, FAT32, exFAT) | wrong reads and saves that get lost. Afterward, macOS's `fsck` checks the image |
| the menu | a launch block for a file on the image | a plugin that can't open its file |
| USB | a pad, keyboard, mouse or listening computer, from the script | how the plugin handles them (USB itself isn't tested) |
| the PSRAM | 8 MB of ordinary memory | the plugin's arithmetic |

The models keep their own clock. It moves on 100 microseconds with each
`time_us_32()` call, and by the time a sleep or the card takes. The
pico-sdk's alarms and repeating timers (`add_repeating_timer_ms()`,
`add_alarm_in_ms()` and the rest of `pico/time.h`) run on that clock. A
callback runs once its time has come, when the plugin next calls the
task function or another of the SDK's console calls, or sleeps. On the
cart it's an interrupt that can come between any two instructions, so
the models show a timer's timing but not its races with the loop.

Then the test plays the plugin's lists to the SDK's real Z80 kernel on
MAME's ColecoVision (`tools/mame-replay.sh`, headless). MAME's picture
has to match the model's pixel for pixel, and for the frame kernel, no
list can make a frame start late. For the frame kernel's controller
reports, `tools/mame-controllers.sh` presses every switch of both MAME
controllers and checks what the kernel tells the cart, with
`CONTROLLER2` and without it.

That step needs `mame`, `sjasmplus` and the ColecoVision BIOS. MAME
calls the BIOS `coleco.zip`, and the kit doesn't include it, so bring
your own. Put it in a folder outside the plugin's repository and name
that folder in `MAME_ROMPATH`:

```sh
MAME_ROMPATH=$HOME/mame/roms test/run.sh
```

You can also put it where MAME already looks: the `rompath` in its
`mame.ini`, or, with no `mame.ini`, a `roms` folder in the folder you run
the test from. If MAME can't find it, `test/run.sh` says so and skips the
MAME step, as `NO_MAME=1` does.

To test your plugin this way, add a block for it to `test/run.sh` by
copying the `hello` one. A plugin made by `tools/new-plugin.py` already
has a block. You can also run it by hand. The script's events are listed
at the top of `test/host/host.c`, and the USB ones are in
docs/features.md:

```sh
test/host/build.sh src /tmp/myplugin
PC_OUT=/tmp PC_END=3000 PC_SCRIPT="500:joy=up 900:key=5" /tmp/myplugin
open /tmp/screen.png; cat /tmp/log.txt
```

The card tests need macOS, for its `fsck` and for mounting the image
afterwards, and the rest also run on Linux. `tools/mkcard.sh FOLDER
IMAGE [fat16|fat32|exfat] [mbr|gpt]` makes a card image from a folder,
for `PC_CARD=IMAGE`. On a Mac it uses `hdiutil` and always names the
volume, since `hdiutil` fails with "Operation not permitted" when it names
it after a folder whose name is longer than 11 characters. On Linux it
uses `mkfs.fat` and `mcopy` (the dosfstools and mtools packages) and
makes FAT16 or FAT32 in an MBR. The simulator takes a folder as the card
on any system.

To play your plugin instead of scripting it, use the simulator. It runs
the same models in a window, with sound, the keyboard, mouse and game
pads, your computer's MIDI ports, and a folder as the SD card
(docs/simulator.md). Its headless mode takes the same scripts, plus a
few extra words for the keyboard, mouse and pads, and `sim/test.sh` runs
every example that way.

## How loud

A plugin should sound about as loud as an ordinary ColecoVision
cartridge with the TV at the same volume. The cart's first arcade
plugins were far too quiet next to one. Compare the two in MAME,
recorded the same way:

```sh
# your plugin: 30 seconds on the Mac with the controls that make it play,
# then MAME records what it sent through the SDK's real kernel
tools/loudness.sh src 30 "300:key=1 400:key="
# a cartridge you own, 60 seconds of its title and attract mode
SDL_VIDEODRIVER=dummy mame coleco -rompath "$MAME_ROMPATH" -cart cart.col \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -skip_gameinfo -seconds_to_run 60 -wavwrite cart.wav
python3 tools/wavlevel.py cart.wav
```

`tools/loudness.sh` works for screen plugins and frame plugins. It
builds the plugin for the Mac, runs it with the script (`PC_SCRIPT` in
`test/host/host.c`), and has headless MAME play what it sent to the real
kernel with `-wavwrite` (`MAME_WAV=plugin.wav tools/mame-replay.sh`).
A frame plugin's sound is in its lists. A screen plugin's goes through
the sound mailbox, so the models write it to `sound.bin` beside
`lists.bin`, and MAME plays it at its times. Give it the folder of a
`test/run.sh` run instead (`tools/loudness.sh
"${TMPDIR:-/tmp}/parrotcart-test/myplugin"`) and it measures that run.

`-sound none` keeps MAME quiet on the computer, and `-wavwrite` still
records. `wavlevel.py` gives each file's level in dBFS, leaving out its
silent seconds. Get the plugin's `rms` within a few dB of the
cartridge's. A `peak` at 0 means the sound clips. `loudness.sh` checks
this with `wavlevel.py --check`: it fails a plugin louder than the
loudest cartridge below, more than 4 dB quieter than their average, or
clipping. The run is only as long as its script, so give it one that
plays the game's sound for a while. Then listen on a TV, which MAME
can't stand in for.

It measures each second from 20 Hz to 15 kHz. A plugin that plays
samples (`examples/samples`) has two things in its recording that aren't
sound: the parked tones' carrier, which MAME folds down to a 16-18 kHz
tone at about -40 dBFS, and the DAC's resting level drifting back at a
few Hz after a loud sound. Unfiltered, both make silent seconds count as
sound, and those seconds pull the level down. `--plain` gives that older
measure. For a cartridge, and for a plugin that's seldom silent, the two
agree within half a dB.

Ordinary cartridges measured this way give an `rms` of -20.5 to -23.9
dBFS (Donkey Kong, Smurf, Lady Bug and Mr. Do!), about -22 on average.
Aim within 3 or 4 dB of that and not above it.

Without the BIOS, MAME can't play anything, but the level can still be
worked out from the run. `loudness.sh` does this when MAME or the BIOS
isn't there, or with `NO_MAME=1`:

```sh
python3 tools/listlevel.py --check "${TMPDIR:-/tmp}/parrotcart-test/myplugin/lists.bin"
```

It times each sound byte as the console would, takes each voice's level
from `tools/ladder.json` (measured on a real console), and prints `rms`,
`median` and `loudest` in the same dBFS as `wavlevel.py` on MAME's
recording. For a screen plugin it reads the `sound.bin` beside
`lists.bin`. On the kit's samples example, on tones, on noise and on
Donkey Kong Jr.'s lists it came within half a dB of MAME, and on the
sound example's mailbox within 0.6 dB.

To match an arcade game's balance instead, record the arcade in MAME
over the same play and measure the plugin over the seconds where the
arcade has sound. `--span` lines the two recordings up by their
envelopes first:

```sh
python3 tools/wavlevel.py --span arcade.wav plugin.wav
```

A plugin that scales its samples up and rounds the peaks off with a soft
knee (`.claude/skills/parrot-cart-plugin/references/porting.md`, "How
loud") can change the balance between its parts. The knee squeezes the
loud effects and leaves the music alone, so the mix reaches its level
with the music louder than in the arcade. Write each part's samples
alone at the same gain (raw 32-bit floats, or 16-bit WAVs), then:

```sh
python3 tools/knee.py music.f32 effects.f32 --gain 1 1.5 2
```

It prints, for each gain, how many samples go into the knee or past full
scale and how much the knee takes off each part. Pick a gain where no
part loses much more than the others.

## Fast enough on the cart

The models and the simulator run far faster than the cart's M33, so they
can't tell you whether a plugin keeps up. A list has 16.77 ms of the
console's time, 2.52 million cycles at 150 MHz. `tools/armcount/` counts
what your code takes over each one, on your computer:

```sh
python3 -m venv ~/armcount-venv
~/armcount-venv/bin/pip install -r tools/armcount/requirements.txt

cp tools/armcount/bench.c test/bench.c       # then put a list's work in bench_list()
tools/armcount/build.sh /tmp/bench.elf test/bench.c src/game.c -- -O2
~/armcount-venv/bin/python3 tools/armcount/armcount.py /tmp/bench.elf --lists 600 --profile
```

`bench.c` is a working example (a picture scaled by 3/4 each list), so
run it once as it is. `build.sh` builds with the pico-sdk's flags,
everything in SRAM; pass your plugin's own flags after `--`.
`--load bench_data=FILE` fills a global from a file before the start,
which is how a bench gets the user's ROMs without the card. The report
gives the average, the worst and the lists that would be late, and
`--profile` the time by function. Leave a few milliseconds to spare: it
can't count what the bus costs when the other core or the DMA uses the
same RAM, so the plugin's own count of late lists on the cart is the
last word.

If you write a counter of your own: Unicorn needs its ARMv8 model
(`UC_CPU_ARM_MAX`) or the M33's `vsel` faults, and after adding or
removing a hook call `ctl_flush_tb()`, or code it translated before runs
uncounted and the numbers come out far too low.

## What you can only test on a console

The models assume a console and devices that behave perfectly. These
things only get tested when your plugin runs on a ColecoVision with the
cart:

- Starting the plugin and going back to the menu, including the menu
  copying the plugin into flash, RESET, and switching the console off
  and on while the plugin runs.
- USB devices, since every one is different. Try hubs, how pads start
  up, and a computer's first "Allow this accessory?" prompt.
- A real SD card, with its start-up time and slow writes, and what
  happens when someone pulls it out.
- The PSRAM chip itself.
- How the sound really sounds, how loud it is next to a cartridge, and
  how the picture looks on a TV, where colors bleed and one-pixel lines
  shimmer.
- The bus. A frame plugin's note can say how many addresses the bus loop
  refused (`cvf_bus_counts()`). A few are normal, and many point at the
  cart's seating or the console.
- Memory, meaning the plugin's real heap and stack. The Mac's are
  bigger.
- Speed. The Mac runs your loop a thousand times faster, so a loop pass
  that takes 30 ms on the cart only shows up as a sluggish screen on the
  console.

## A test plan for the examples on a console

Copy the eleven `.CVP` files to the card's `/PLUGINS` folder, and put a text
file or two anywhere on the card. Check these steps for every plugin
first:

| # | Do | Expect |
|---|---|---|
| A | Go to Home, then the Plugin Manager | Each plugin is listed with its icon in magenta, its name and its version. Text Viewer, which needs a file, has the plug icon |
| B | Press A on it, then `*` | Its details (version, developer `Parrot Cart` or yours, ID, what it uses), then its README, 30 columns wide, with pages turning |
| C | Press B, then A | The copy into flash counting up the first time, then the plugin's screen within a second or two |
| D | Press RESET | The menu, back in the Plugin Manager, with the plugin's note on the bottom line |
| D2 | Start it again. Hold both fire buttons and `*` for 2 seconds, then do it again with Select and Start on a USB pad | The menu each time, as after RESET. Held for only a second, nothing happens |
| E | Start it again. Switch the console off, wait 5 seconds, and switch it on | The menu, as after a cold start |

Then check each plugin:

| Plugin | Do | Expect |
|---|---|---|
| Hello (`HELLO.CVP`) | Use the stick, both fire buttons and each keypad key | Each one is named on the TV while you hold it, and `PRESSES` counts them. After RESET, the menu says `Hello: N presses` |
| Screen Example | Watch it, then press fire | The ball moves smoothly inside its black box, with no trail and no flicker in the parts that don't move. Fire changes its color. The 15 swatches are 15 different colors. The bricks are red on gray, and the coins are yellow |
| Sound Example | Press keypad 1 to 8, move the stick up and down, and press left fire | A C major scale, in tune, with each note stopping when you let go of the key. The stick moves the octave up and down. Left fire plays a drum hit that fades in a third of a second. There are no clicks or stuck notes, and it's silent after RESET |
| Game Pad Example | Start with nothing plugged in. Then plug in a powered hub with a pad, press everything, move both sticks to their corners, and pull the pad out and put it back | `NO PAD YET` at first, then `SWITCH PRO` or `HID PAD` within a few seconds. Each button lights its name, under the right name for where it is (the bottom face button is `SOUTH`). Each dot follows its stick, rests near the center (numbers near 0) and reaches about +-2047 at the edges, with up being positive. `DIRECTIONS` follows the d-pad and the left stick. Unplugging goes back to `NO PAD YET` without freezing, and plugging in works again |
| Keyboard Example | Plug a keyboard and a mouse into the powered hub. Type, press Enter and Backspace, move the mouse and click | `KEYBOARDS 1 MICE 1`. Letters appear as you type, with none lost or doubled. Enter moves the lines up. The pointer follows the mouse to all four edges of the blue field, and turns red (left button) or green (right button) |
| MIDI Out Example | Plug the cart's USB-C into a Mac and allow the accessory if asked. Open Audio MIDI Setup or GarageBand, and press keypad 1 to 8 | The TV says `A COMPUTER IS LISTENING`, and the Mac lists `Parrot Cart MIDI Example`. Each key plays a note that stops when you let go, and `SENT` counts two for each key. With nothing plugged in, the TV says `PLUG THE CART INTO A COMPUTER`, and the keypad still counts without hanging |
| SD Card Example | Run it, press RESET, run it again, then look at the card on a computer | The top folder's first 8 entries with their sizes, the first file's first characters, and `RUN 1, SAVED AND READ BACK`, then `RUN 2`. The menu's note gives the run. On the computer, `/SDCARD.SAV` is 512 bytes and the card checks clean. Try a FAT32 card and an exFAT card. If it can't read the card, it says so in red |
| Text Viewer | In the card's folders, press A on a `.txt` file (it has the plugin's green icon). Move the stick down and up, hold left fire and move the stick, then press RESET | The file's name and text, wrapped at 30 columns. The stick scrolls by a line, and by a page with left fire held. Back in the menu, you see the same folder with the same file selected, and `Read NAME` on the bottom line. In the Plugin Manager, its details say it starts with a file, and A there doesn't start it. A name with an accented letter still opens |
| Frames Example | Watch for a minute, and steer with the stick | Eight color bars appear within a quarter of a second. A round white ball moves smoothly, with no tearing, no stray blocks left behind and no stutter. There's a short beep at each bounce and silence between, with no drone, and the stick turns the ball |
| Two Controllers Example | Plug a second controller into the console's port 2. Move each stick all eight ways, press each fire button and each keypad key, on one controller and then the other. Then plug a pad into a powered hub and move both its sticks | Each controller's grid lights the way its stick points, `FIRE L` and `FIRE R` turn red while held, and the key held lights on its keypad. Nothing shows on the other controller's side. `TWIN STICK` shows controller 1's stick and the pad's left stick under `MOVE`, and controller 2's stick, the pad's right stick or its face buttons under `FIRE`. After RESET, the menu shows the last values, `C1 JF F3 KF, C2 JF F3 KF` with nothing held |
| PSRAM Example | Run it | `ID 0D 5D ...`, the bar filling twice, then `ALL 8 MB READ BACK RIGHT` and two times in milliseconds. Write the times down, because they're the PSRAM's real speed |

If one fails, look first at the menu's note after RESET, and at the
handoff trace on the menu's USB serial (`t`).
