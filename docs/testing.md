# Testing

## On your computer

```sh
test/run.sh                    # the template's plugin and every example
test/run.sh hello sound        # some of them
NO_MAME=1 test/run.sh          # without the MAME step
```

Each plugin's own `main.c` is compiled for your computer and run against
models of everything on the cart (`test/host/host.c`):

| The model | What it does | What it catches |
|---|---|---|
| the screen kernel | runs each list a frame after it is sent, answers HELLO and ACK, reports the controller from a script | a malformed list, a list sent too early, three colors in an 8-pixel run, a screen the SDK and the console disagree about |
| the frame kernel | takes one list a TV frame and adds up its time by the kernel's costs | a list that outlasts its frame or its vertical blank, bad commands |
| the card | a disk image through the SDK's own file system (FAT16, FAT32, exFAT) | wrong reads, saves that don't survive; macOS's `fsck` then checks the image |
| the menu | a launch block for a file on the image | a plugin that can't open its file |
| USB | a pad, keyboard, mouse or listening computer, from the script | the plugin's handling of them, not USB itself |
| the PSRAM | 8 MB of ordinary memory | the plugin's arithmetic |

Then the lists the plugin sent are played to the SDK's **real Z80
kernel** on MAME's ColecoVision (`tools/mame-replay.sh`, headless), and
MAME's picture must equal the model's pixel for pixel; for the frame
kernel, no list may make a frame start late.

To test your own plugin this way, add a block for it to `test/run.sh`
(copy the `hello` one; a plugin made by `tools/new-plugin.py` has its
own), or run it by hand. The script's events are listed at the top of
`test/host/host.c`, and the USB ones in docs/features.md:

```sh
test/host/build.sh src /tmp/myplugin
PC_OUT=/tmp PC_END=3000 PC_SCRIPT="500:joy=up 900:key=5" /tmp/myplugin
open /tmp/screen.png; cat /tmp/log.txt
```

The card tests need macOS (`hdiutil`); the rest runs on Linux too.

## What only the console can tell you

The models are of a console that behaves. None of this is tested until it
has run on a ColecoVision with the cart:

- **Starting and going back**: the menu copying the plugin into flash,
  the switch between the menu and the plugin, RESET, the console switched
  off and on while the plugin runs.
- **USB**: every device is different. Hubs, pads' start-up, a computer's
  first "allow this accessory?".
- **The real card**: its start-up time, slow writes, a card pulled out.
- **The PSRAM chip.**
- **Sound**, as it sounds; and **the picture on a TV**: colors that
  bleed, one-pixel lines that shimmer.
- **Memory**: a plugin's real heap and stack (the Mac's are bigger).
- **Speed**: the Mac runs your loop a thousand times faster. A loop pass
  that takes 30 ms on the cart shows as a sluggish screen there and
  nowhere else.

## The examples on a console: a test plan

Copy the ten `.CVP` files to the card's `/PLUGINS`, and
a text file or two anywhere on it. For every plugin, first:

| # | Do | Expect |
|---|---|---|
| A | Home, Plugins | each is listed with its icon in magenta, its name and version |
| B | A on it | its details: version, developer `Parrot Cart` (or yours), ID, what it uses, and its README, 30 columns, pages turning |
| C | A again | a progress bar the first time, then the plugin's screen within a second or two |
| D | RESET | the menu, on the Plugins page, with the plugin's note on the bottom line |
| E | start it again; switch the console off, wait 5 seconds, on | the menu, as after a cold start |

Then each one's own:

| Plugin | Do | Expect |
|---|---|---|
| Hello (`HELLO.CVP`) | stick, both fire buttons, each keypad key | each is named on the TV while held; `PRESSES` counts; after RESET the menu says `Hello: N presses` |
| Screen Example | watch; press fire | the ball moves smoothly inside its black box, no trail, no flicker in the static parts; fire changes its color; the 15 swatches are 15 different colors; bricks red on gray, coins yellow |
| Sound Example | keypad 1 to 8; stick up, down; left fire | a C major scale, in tune, each note stopping when the key is let go; octave up and down; a drum hit that fades in a third of a second; no clicks or stuck notes; silence after RESET |
| Game Pad Example | nothing plugged in; then a powered hub with a pad; press everything; move both sticks to their corners; pull the pad out and put it back | `NO PAD YET`; within a few seconds `SWITCH PRO` or `HID PAD`; each button lights its name, under the right name for where it is (bottom face button = `SOUTH`); each dot follows its stick, rests near the center (numbers near 0) and reaches about +-2047 at the edges, up being positive; `DIRECTIONS` follows the d-pad and the left stick; unplugging goes back to `NO PAD YET` with no freeze, and plugging in works again |
| Keyboard Example | a keyboard and a mouse on the powered hub; type; Enter; Backspace; move the mouse; click | `KEYBOARDS 1 MICE 1`; letters appear as typed with none lost or doubled; Enter moves the lines up; the pointer follows the mouse to all four edges of the blue field and turns red (left) or green (right) |
| MIDI Out Example | the cart's USB-C into a Mac (allow the accessory if asked); open Audio MIDI Setup, or GarageBand; keypad 1 to 8 | the TV says `A COMPUTER IS LISTENING`; the Mac lists `Parrot Cart MIDI Example`; each key plays a note that stops on release; `SENT` counts two a key. Then with nothing plugged in: `PLUG THE CART INTO A COMPUTER`, and the keypad still counts with nothing hanging |
| SD Card Example | run it; RESET; run it again; then look at the card on a computer | the top folder's first 8 entries with sizes, the first file's first characters, `RUN 1, SAVED AND READ BACK` and then `RUN 2`; the menu's note says the run; on the computer `/SDCARD.SAV` is 512 bytes and the card checks clean. Try a FAT32 and an exFAT card. With no card readable it says so in red |
| Text Viewer | in the card's folders, A on a `.txt` file (it has the plugin's green icon); stick down, up; left fire + stick; RESET | the file's name and text, wrapped at 30 columns; scrolling by a line and by a page; back in the menu the same folder with the same file selected and `Read NAME` on the bottom line. Started from the Plugins page instead: `OPEN A .TXT FILE FROM THE MENU`. A name with an accented letter still opens |
| Frames Example | watch for a minute; steer with the stick | eight color bars painted in within a quarter of a second; a round white ball moving smoothly, with no tearing, no stray blocks left behind and no stutter; a short beep at each bounce; the stick turns it |
| PSRAM Example | run it | `ID 0D 5D ...`, the bar filling twice, then `ALL 8 MB READ BACK RIGHT` and two times in milliseconds (note them: they are the PSRAM's real speed) |

If one fails, the menu's note after RESET, and the handoff trace on the
menu's USB serial (`t`), are where to look first.
