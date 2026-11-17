# Getting started

A Parrot Cart plugin is a program for the cart's own processor (an RP2350,
the chip of the Raspberry Pi Pico 2), packed into one `.CVP` file. The
cart's menu finds it in the SD card's `/PLUGINS` folder and starts it. You
build it on a Mac or a Linux machine with the same free tools a Pico
project uses.

## 1. Tools

| Tool | Version | For |
|---|---|---|
| Arm GNU toolchain (`arm-none-eabi-gcc`) | 9 or later (developed with 9.2.1) | compiling for the cart |
| CMake, Ninja | CMake 3.13 or later | the build |
| Python 3 | 3.8 or later, no packages | packing the `.CVP`, the README, the tools |
| [sjasmplus](https://github.com/z00m128/sjasmplus) | 1.20 or later (developed with 1.23.1) | assembling the SDK's Z80 kernels |
| [pico-sdk](https://github.com/raspberrypi/pico-sdk) | **2.1.1** | the chip's SDK |
| picotool | **2.3.1** (2.1.1 makes a different, 512 KB image) | packaging. Install it: without one, pico-sdk 2.1.1 downloads and builds picotool's development branch |

macOS, with [Homebrew](https://brew.sh):

```sh
brew install cmake ninja sjasmplus picotool
brew install --cask gcc-arm-embedded
```

Debian or Ubuntu:

```sh
sudo apt install cmake ninja-build python3 build-essential \
    gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
# sjasmplus isn't packaged: build it
git clone --depth 1 -b v1.23.1.1 --recurse-submodules https://github.com/z00m128/sjasmplus
make -C sjasmplus && sudo make -C sjasmplus install
# and picotool, from https://github.com/raspberrypi/picotool (tag 2.3.1),
# built against pico-sdk 2.3.1 with its mbedtls, as tools/ci/setup.sh does
```

`tools/ci/setup.sh` is those steps exactly, as the GitHub workflows run
them on Ubuntu 24.04.

The pico-sdk, anywhere you like (its submodules aren't needed: the cart's
SDK brings its own TinyUSB):

```sh
git clone --depth 1 -b 2.1.1 https://github.com/raspberrypi/pico-sdk ~/pico-sdk
```

## 2. Build

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

The last line it prints is the plugin file:

```
build/HELLO.CVP: Hello 0.1.0 (yourname.hello), image 55552 bytes from 0x10e00000
```

(`export PICO_SDK_PATH=$HOME/pico-sdk` in your shell's profile saves
typing it. `-DPICO_SDK_FETCH_FROM_GIT=ON` instead downloads 2.1.1 into the
build folder.)

The examples build the same way, all at once:

```sh
cmake -S examples -B build-examples -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build-examples
```

## 3. On the cart

1. Put the SD card in your computer and copy the `.CVP` into its
   `/PLUGINS` folder (make the folder if the card has none).
2. Card back in the cart, console on. In the menu, B from the card's top
   folder goes to Home; **Plugins** lists what is in `/PLUGINS`, each with
   its icon, name and version.
3. A on your plugin shows its details: version, developer, ID, what it
   opens, and its README. A again starts it. The first time, the cart
   copies it into its own flash and shows a progress bar; after that it
   starts at once.
4. RESET on the console goes back to the menu.

A plugin that opens files (`OPENS .txt`) also starts when you pick such a
file in the card's folders and press A.

To put a new build on the card, copy the new file over the old one. The
cart notices that it changed and copies it again. To remove a plugin,
delete its file, or use `#` on its details page.

You never flash the cart from a computer to run a plugin, and a plugin
that crashes can't harm the cart: RESET, or off and on, is back in the
menu.

## 4. Make it yours

In `CMakeLists.txt`:

```cmake
cvcart_plugin(hello ID yourname.hello NAME "Hello" VERSION 0.1.0 BY "Your Name"
              ICON src/icon.txt README README.md)
```

- `ID`: pick it once and never change it. `docs/plugin-rules.md`.
- `ICON`: `docs/icons.md`.
- `README`: `docs/readme-spec.md`.
- The target's name (`hello`, three times in the file) names the output:
  `HELLO.CVP`. Or say `FILE MYPLUGIN.CVP`.

Then `src/main.c`. Read it from the top: it is the shape every plugin
has. `examples/README.md` says which example shows what, and
`sdk/firmware/revc/sdk/README.md` is the SDK's reference.

## 5. Try it without the console

```sh
sim/run.sh src             # your plugin in a window, with sound and controls
test/run.sh hello          # your plugin, on a model of the console
tools/screenshot.sh shot.png src 3000 "500:joy=up 1000:key=5"
```

The simulator needs SDL3: `docs/simulator.md` says how to install it, and
which keys are the controller. `docs/testing.md` has the rest, and what
only a real console can tell you.

## Things that go wrong

| What you see | Why |
|---|---|
| `sjasmplus: command not found` during the build | install sjasmplus (above) |
| `the plugin's .bss reaches the switch's reserved RAM` | the plugin's data is too big for the chip's RAM: move big buffers to the PSRAM (`examples/psram`) |
| `links malloc: its heap would grow into the launch block` | something calls `malloc` (or `printf` of a float, `new`, ...). Avoid it, or add `HEAP` to `cvcart_plugin()` (`sdk/.../cvheap.h`) |
| a warning that the pico-sdk isn't 2.1.1 | it will probably work; 2.1.1 is what the cart is built and tested with |
| on the TV: `NEEDS NEWER CART FIRMWARE` | the plugin was built with a newer SDK than the cart's menu knows: update the cart |
| the plugin isn't listed | the file isn't in `/PLUGINS`, or isn't a `.CVP` the menu can read (it is then listed by its file name) |
