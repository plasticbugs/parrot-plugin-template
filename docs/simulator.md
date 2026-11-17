# The simulator

The simulator runs your plugin on your computer, in a window, with sound
and controls. It builds your plugin's own C files for the computer, against
models of the console and the cart (the same models `test/run.sh` uses).
Then you play it with the keyboard or a game pad.

```sh
sim/run.sh src                       # the template's plugin
sim/run.sh examples/sound            # an example
sim/run.sh src --card ~/sdcard       # with a folder as the SD card
sim/run.sh src --usb host            # USB pads, a keyboard and a mouse on the cart
```

The first build takes a few seconds; after that, only what changed is
built again. Use it to try ideas fast. Then try the plugin on a real
console before you release it: see "What the simulator can't tell you"
below.

## 1. Install SDL3

The simulator needs CMake, a C compiler, and [SDL3](https://libsdl.org)
(3.2 or later).

macOS, with [Homebrew](https://brew.sh):

```sh
brew install sdl3 cmake ninja
```

Linux: Ubuntu 25.04 and later, and Debian 13, have SDL3:

```sh
sudo apt install libsdl3-dev cmake ninja-build build-essential libasound2-dev
```

Older ones (Ubuntu 24.04, Debian 12) don't. Install SDL's build needs,
then build SDL into the kit's `build-sim/sdl3` (a few minutes, once):

```sh
sudo apt install cmake ninja-build build-essential libx11-dev libxext-dev \
    libwayland-dev libxkbcommon-dev libegl-dev libasound2-dev libpulse-dev libudev-dev
sim/get-sdl.sh
```

On Linux, `libasound2-dev` is for MIDI ports (section 4). Without it, the
simulator still builds, with no MIDI ports.

Windows, with Visual Studio 2022 (the "Desktop development with C++"
workload, which has CMake): in Git Bash, `sim/get-sdl.sh` puts the SDL
team's own Windows build in `build-sim\sdl3`. Or use
[vcpkg](https://vcpkg.io): `vcpkg install sdl3`, then set
`CMAKE_TOOLCHAIN_FILE` to vcpkg's `scripts\buildsystems\vcpkg.cmake`.

Windows, with [MSYS2](https://www.msys2.org), in its UCRT64 shell:

```sh
pacman -S mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gcc
```

## 2. Run

```sh
sim/run.sh PLUGIN_DIR [options]
```

`PLUGIN_DIR` is the folder with your plugin's `.c` and `.cpp` files. On
Windows, `sim\run.bat PLUGIN_DIR [options]` does the same from the
command prompt, and `sim/run.sh` works in Git Bash and MSYS2.

`sim/run.sh src --help` lists every option. The ones you'll use most:

| Option | What it does |
|---|---|
| `--card FOLDER` | the SD card: the folder, as the plugin sees a card (section 5) |
| `--open PATH` | start as if `PATH` had been picked in the cart's menu (section 5) |
| `--usb host` | the cart's USB port as a host, for pads, keyboards and mice (section 4) |
| `--midi-in NAME`, `--midi-out NAME` | which MIDI ports the plugin uses (section 4) |
| `--tv` | the TV's shape: a border, and the picture 4:3 |
| `--scale N` | window pixels for each console pixel (default 3) |
| `--mute` | no sound |
| `--strict` | stop at the first rule the plugin breaks (section 6) |
| `--shots DIR` | where F12 saves screenshots |

If your plugin needs more files to build, give them before the
simulator's options:

```sh
sim/run.sh ../my-plugin/src --src '../vendor/lib/*.c' \
    --include ../my-plugin/vendor/lib --define USE_THIS=1
```

`--src` takes a file or pattern, relative to `PLUGIN_DIR`. `--include`
adds a folder for `#include`, and `--define` a define. `--no-sanitize`
builds without the memory checks (section 6), for speed.

From `sim\run.bat`, or with your own build, use CMake directly:

```sh
cmake -S sim -B build-sim/myplugin -DPLUGIN_DIR=/path/to/my-plugin/src \
    "-DPLUGIN_SOURCES=../vendor/lib/*.c" -DPLUGIN_INCLUDES=/path/to/vendor/lib
cmake --build build-sim/myplugin --config Release
build-sim/myplugin/parrotsim --card ~/sdcard
```

## 3. Keys

| Key | On the console |
|---|---|
| arrows, or W A S D | the controller's stick |
| Z, X | its left and right fire buttons |
| 0 to 9 | its keypad's digits |
| `-` and `=` | its keypad's `*` and `#` |

Any game pad works too. Its d-pad or left stick is the controller's stick,
its face buttons are the fire buttons, and Back and Start are `*` and `#`.
Game pads are also the plugin's USB pads (section 4).

| Key | In the simulator |
|---|---|
| F1 | list the keys in the terminal |
| F2 | change the keyboard's role (below) |
| F5 | the console's RESET |
| F12 | a screenshot |
| Esc | quit (so does closing the window) |

F2 moves the keyboard between three roles:

1. **The controller** (the default): the keys above.
2. **USB pad 1**, for a plugin that reads one when you have no pad:
   arrows for the d-pad, Z X A S for the face buttons (south, east, west,
   north), Q W for L and R, E R for ZL and ZR, Enter and Backspace for +
   and -, H for Home, I J K L for the left stick.
3. **A USB keyboard and mouse**: every key goes to the plugin as a USB
   keyboard's, and the mouse is a USB mouse (section 4). The window keeps
   the mouse until you press F2 again. Esc goes to the plugin in this
   role. Only F1, F2, F5 and F12 stay the simulator's.

The window's title always says which role the keyboard has. After F2,
and when the simulator starts, a label in the window's bottom left corner
says it too, for three seconds.

**RESET** (F5) does what the console's RESET does: the plugin's session
ends, the card is brought up to date, and the window shows the line your
plugin left for the menu (`cvk_note()`). F5 or a fire button there starts
the plugin again, from the start, as a fresh program. Esc quits.

## 4. USB: pads, a keyboard, a mouse, MIDI

A plugin turns on the cart's USB features with `cvcart_features()` in its
`CMakeLists.txt` (`docs/features.md`). The simulator reads that line and
gives the plugin what it asks for.

**The USB port.** On the cart, the port is either a computer's MIDI
device or a host for a hub (`cvusb_mode()`). The simulator chooses as the
cart does. A plugin with `MIDI_OUT` finds a computer listening, and
becomes its MIDI device after 0.3 seconds. Any other plugin is a host.
`--usb` changes that:

| Option | On the cart's USB port |
|---|---|
| `--usb device` | a computer that is listening (the default) |
| `--usb host` | a hub, and no computer: after 1.5 seconds the port is a host |
| `--usb ask` | a computer that asks its user first, and gets no answer |

Pads, keyboards and mice only reach a plugin whose port is a host. So for
a plugin with `MIDI_OUT`, start the simulator with `--usb host` to try
them. F2 reminds you in the terminal.

**Game pads** (`PADS`). The first two game pads are the plugin's USB pads
1 and 2, with all their buttons and both sticks. A Switch Pro Controller
is a `SWITCH PRO` to `cvusb_pad_kind()`, and any other pad is a
`HID PAD`. As on the cart, a pad keeps its number until you unplug it:
unplug pad 1, and pad 2 is still pad 2. The next pad you plug in becomes
pad 1. A third pad isn't used. Pad 1 is also the console's controller
(or pad 2, while there is no pad 1).

**A keyboard and a mouse** (`KEYBOARD`, `MOUSE`). In F2's third role,
each key goes to the plugin as a USB keyboard sends it. The SDK's own
code reads it (`cvusb_keys.c`), so `cvusb_key()` gives the same usage,
character and modifiers as on the cart. Keys don't repeat, as a USB
keyboard's don't. The mouse moves, clicks and scrolls as a USB mouse.
When you press F2 again, or the window loses focus, the plugin sees
every key and button let go.

**MIDI** (`MIDI_IN`, `MIDI_OUT`). The plugin's MIDI goes to and from your
computer's MIDI ports: CoreMIDI on macOS, ALSA on Linux, and WinMM on
Windows.

- **Out**: on a computer, the cart is a MIDI device named after the
  plugin. On macOS and Linux, the simulator makes a MIDI port with the
  plugin's name. Your music programs see it as an input and an output.
  What the plugin sends comes out of it, and what they send to it reaches
  the plugin. Windows has no such ports, so use `--midi-out NAME` to send
  to a port that is there: a synth, or a port made with
  [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html).
- **In**: on the cart, a MIDI keyboard plugs into the hub. The simulator
  reads every MIDI input on your computer, or only the one you name with
  `--midi-in NAME`.

| Option | What it does |
|---|---|
| `--midi-in NAME` | read the MIDI input called `NAME` (or with `NAME` in its name). `all` reads every input (the default with `MIDI_IN`), and `none` reads none |
| `--midi-out NAME` | send the plugin's MIDI to the output called `NAME`. `virtual` makes the plugin's own port (the default with `MIDI_OUT`, on macOS and Linux), and `none` sends it nowhere |
| `--midi-log FILE` | write each message the plugin sends to `FILE`, with the time in milliseconds |
| `--midi-list` | list the MIDI ports, and stop |

F1 says which MIDI ports are open. With `--usb host`, what the plugin
sends to the MIDI device on the hub goes to the same output.

The plugin's MIDI goes out once a TV frame, so its timing can be up to
1/60 of a second off. Input reaches the plugin once a frame too. SysEx
doesn't reach the plugin, as on the cart.

## 5. The SD card

`--card FOLDER` turns a folder on your computer into the plugin's SD card.
The plugin sees a FAT32 card through the SDK's own file system code, with
the folder's files and subfolders on it. Files are read from your folder
when the plugin reads them.

What the plugin writes lands back in the folder when the session ends:
RESET, Esc, or closing the window. A save file the plugin makes
(`fs_create()`, then `fs_write_sec()`) shows up as a new file in the
folder; a file it changes is changed there. Leave the folder alone while
the simulator runs.

A few things about the folder:

- Files and folders whose names start with `.` aren't on the card.
- Files of 4 GB or more aren't on the card (FAT32 can't hold them).
- The card has room to spare beyond the files, for new ones. The terminal
  says its size when it starts.

`--card` also takes a disk image (a `.img` file, say) of a whole card.
The plugin reads and writes that file directly.

`--open PATH` starts the plugin as if `PATH` had been picked in the cart's
menu, so `cvsd_open_launched()` opens it. `PATH` is either a path on the
card (`--open /NES/game.nes`) or a file on your computer. For a file on
your computer, its folder becomes the card, unless `--card` says
otherwise. (In Git Bash, which turns a leading `/` into a Windows path,
leave the slash off: `--open NES/game.nes`.)

```sh
sim/run.sh examples/open-file --open ~/Documents/notes.txt
```

## 6. Problems the models find

The models check the same rules `test/run.sh` checks: a list for the
console that is malformed, too long, or sent too early; a frame list that
outlasts its TV frame. Each problem is printed in the terminal, a red mark
shows for a second in the window's corner, and the window's title counts
them. The plugin keeps running. With `--strict`, the simulator stops at
the first one.

On macOS and Linux, the simulator is built with the compiler's memory
checks (AddressSanitizer and UndefinedBehaviorSanitizer). A plugin that
writes past the end of an array, uses freed memory, or overflows a signed
number stops with a report that names the line. On the cart, the same bug
just does something odd later. `--no-sanitize` builds without the checks.

Each session also leaves a log of what the plugin did in
`build-sim/NAME/out/log.txt`, with `screen.png`, the last picture.

## 7. Screenshots

F12 saves the picture as `screenshot-1.png`, then `screenshot-2.png`, and
so on, in the current folder (or `--shots DIR`). They are 256x192, the
console's own pixels, which is what the plugin registry wants
(`docs/screenshots.md`).

## 8. Without a window: tests

`--headless` runs with no window and no sound, as fast as the computer
can, and always ends by itself. It is for tests and scripts.

```sh
sim/run.sh src --headless --ms 3000 --script "500:joy=up 900:key=5 1200:key=" \
    --shot hello.png --wav hello.wav
```

| Option | What it does |
|---|---|
| `--frames N` | stop after N TV frames (the default is 600: 10 seconds) |
| `--ms N` | stop after N milliseconds of console time |
| `--script S` | the inputs, in `test/run.sh`'s script language (docs/testing.md), and the words below |
| `--shot FILE.png` | the last picture |
| `--wav FILE.wav` | the sound (48 kHz, 16-bit, mono) |
| `--midi-log FILE` | what the plugin sent over MIDI (section 4) |

Headless, the simulator opens no MIDI ports unless `--midi-in` or
`--midi-out` names one, so a test never touches your MIDI devices. Use
`--midi-log` to see what the plugin sent, and the script's `midiin=` word
to send it MIDI. With a port open, a headless run goes at the console's
own speed instead of as fast as it can.

The script's words from `docs/testing.md` (`pad=`, `keydown=`, `mouse=`,
`midiin=` and the rest) go straight to the models. These words go through
the simulator's own keyboard, mouse and game pad code instead, as a
window's would. They are only for the simulator:

| Word | What it does |
|---|---|
| `simrole=controller`, `pad` or `keyboard` | the keyboard's role, as F2 sets it |
| `simkey=04`, `simkeyup=04` | a key goes down or up: its USB usage, in hex (`04` is A, `28` is Enter, `E1` is left Shift) |
| `simmouse=DX,DY,BUTTONS[,WHEEL]` | the mouse moves and its buttons are held (1 left, 2 right, 4 middle); the wheel turns |
| `simpad=switch` or `hid` | a game pad is plugged in: a Switch Pro Controller, or another pad |
| `simbuttons=N:south+l` | the Nth pad plugged in holds these buttons (the names `pad=` uses); `simbuttons=N:` lets go of all |
| `simsticks=N:LX,LY,RX,RY` | the Nth pad's sticks, as the computer reads them: -32768 to 32767, with y down |
| `simunplug=N` | the Nth pad plugged in is unplugged |

While a `simpad=` pad is plugged in, it is the console's controller, as a
real pad is, and the script's `joy=` and `key=` don't reach the console.

```sh
sim/run.sh test/usb --headless --usb host --ms 3000 \
    --script "1600:simrole=keyboard 1620:simkey=0B 1640:simkeyup=0B 1700:simpad=switch 1750:simbuttons=1:south"
```

`sim/test.sh` checks the simulator itself this way: it runs the template's
plugin and every example headless, checks their logs and pictures against
`test/run.sh`'s, checks that the Sound example's scale is in tune in the
WAV and that the Frames example beeps when its lists say, and checks that a
save lands in a card folder. It also runs `test/usb`, a plugin with every
USB feature on, through the keyboard, mouse, pads and MIDI.

## What the simulator can't tell you

The simulator shows you what your plugin draws and plays, and how it
answers the controls. It doesn't show you how it runs on the cart:

- **Speed.** Your computer runs the plugin's code many times faster than
  the cart's chip. The simulator holds the console to 60 frames a second,
  but a loop that is too slow on the cart looks fine here. Only a console
  shows it.
- **Memory.** On the cart, a plugin's code, data and heap share about
  507 KB, and its stack is 4 KB (`docs/hardware.md`). Your computer has
  far more, so a plugin that runs out on the cart may run fine here.
- **Real USB devices.** A desktop game pad becomes an ideal USB pad, and
  your computer's keyboard, mouse and MIDI ports ideal USB ones. The
  cart's USB code (TinyUSB) doesn't run, so hubs, a device's start-up,
  and devices it doesn't know aren't tested. Nor is MIDI's timing: here
  it moves once a TV frame.
- **The TV.** The colors are the video chip's own; a TV blurs and bleeds
  them. Sound comes from a model of the console's sound chip, close but
  not exact.
- **The card's timing.** Reads and writes take a fixed time here. A real
  card is slower, and some are much slower.
- **The cart's start-up**: the menu copying the plugin, and the switch
  between the menu and the plugin.

`docs/testing.md` has the rest of the testing story, including a test plan
for a real console.
