# The simulator

The simulator runs your plugin on your computer, in a window, with sound
and controls. It builds your plugin's own C files for the computer, against
models of the console and the cart (the same models `test/run.sh` uses).
Then you play it with the keyboard or a game pad.

```sh
sim/run.sh src                       # the template's plugin
sim/run.sh examples/sound            # an example
sim/run.sh src --card ~/sdcard       # with a folder as the SD card
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
sudo apt install libsdl3-dev cmake ninja-build build-essential
```

Older ones (Ubuntu 24.04, Debian 12) don't. Install SDL's build needs,
then build SDL into the kit's `build-sim/sdl3` (a few minutes, once):

```sh
sudo apt install cmake ninja-build build-essential libx11-dev libxext-dev \
    libwayland-dev libxkbcommon-dev libegl-dev libasound2-dev libpulse-dev libudev-dev
sim/get-sdl.sh
```

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
| `--card FOLDER` | the SD card: the folder, as the plugin sees a card (section 4) |
| `--open PATH` | start as if `PATH` had been picked in the cart's menu (section 4) |
| `--tv` | the TV's shape: a border, and the picture 4:3 |
| `--scale N` | window pixels for each console pixel (default 3) |
| `--mute` | no sound |
| `--strict` | stop at the first rule the plugin breaks (section 5) |
| `--shots DIR` | where F12 saves screenshots |

If your plugin needs more files to build, give them before the
simulator's options:

```sh
sim/run.sh ../my-plugin/src --src '../vendor/lib/*.c' \
    --include ../my-plugin/vendor/lib --define USE_THIS=1
```

`--src` takes a file or pattern, relative to `PLUGIN_DIR`. `--include`
adds a folder for `#include`, and `--define` a define. `--no-sanitize`
builds without the memory checks (section 5), for speed.

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

The first game pad is also the plugin's USB game pad 1, with all its
buttons and both analog sticks, as `cvpad` reads one on the cart.

| Key | In the simulator |
|---|---|
| F1 | list the keys in the terminal |
| F2 | change the keyboard's role (below) |
| F5 | the console's RESET |
| F12 | a screenshot |
| Esc | quit (so does closing the window) |

F2 moves the keyboard between three roles:

1. **The controller** (the default): the keys above.
2. **A USB game pad**, for a plugin that reads one when you have no pad:
   arrows for the d-pad, Z X A S for the face buttons (south, east, west,
   north), Q W for L and R, E R for ZL and ZR, Enter and Backspace for +
   and -, H for Home, I J K L for the left stick.
3. **A USB keyboard and mouse**: every key goes to the plugin as a USB
   keyboard's, and the mouse is a USB mouse. The window keeps the mouse
   until you press F2 again. Esc goes to the plugin in this role.

The window's title says which role the keyboard has.

**RESET** (F5) does what the console's RESET does: the plugin's session
ends, the card is brought up to date, and the window shows the line your
plugin left for the menu (`cvk_note()`). F5 or a fire button there starts
the plugin again, from the start, as a fresh program. Esc quits.

## 4. The SD card

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

## 5. Problems the models find

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

## 6. Screenshots

F12 saves the picture as `screenshot-1.png`, then `screenshot-2.png`, and
so on, in the current folder (or `--shots DIR`). They are 256x192, the
console's own pixels, which is what the plugin registry wants
(`docs/screenshots.md`).

## 7. Without a window: tests

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
| `--script S` | the inputs, in `test/run.sh`'s script language (docs/testing.md) |
| `--shot FILE.png` | the last picture |
| `--wav FILE.wav` | the sound (48 kHz, 16-bit, mono) |

`sim/test.sh` checks the simulator itself this way: it runs the template's
plugin and every example headless, checks their logs and pictures against
`test/run.sh`'s, checks that the Sound example's scale is in tune in the
WAV and that the Frames example beeps when its lists say, and checks that a
save lands in a card folder.

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
- **Real USB devices.** A desktop game pad becomes an ideal USB pad. The
  cart's USB code (TinyUSB) doesn't run, so hubs, a pad's start-up, and
  devices it doesn't know aren't tested.
- **The TV.** The colors are the video chip's own; a TV blurs and bleeds
  them. Sound comes from a model of the console's sound chip, close but
  not exact.
- **The card's timing.** Reads and writes take a fixed time here. A real
  card is slower, and some are much slower.
- **The cart's start-up**: the menu copying the plugin, and the switch
  between the menu and the plugin.

`docs/testing.md` has the rest of the testing story, including a test plan
for a real console.
