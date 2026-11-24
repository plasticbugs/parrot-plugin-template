# Examples

Each example is a small plugin that shows one thing a plugin can do. Each
one has a `main.c` to read from the top, an icon, a README and five lines
of CMake.

| Folder | Shows | SDK |
|---|---|---|
| `../src` | the smallest plugin, with text, the console's controller and a note for the menu | `cvkernel`, `cvscreen`, `lib/wired.h` |
| `screen` | text at two sizes, the 15 colors, a map of 8x8 tiles, and something moving | `cvscreen`, `vdp.h` |
| `sound` | the sound chip, with notes on one voice and a drum on the noise voice | `cvk_sound`, `cvk_mb` |
| `usb-gamepad` | a USB pad, with buttons, calibrated sticks and directions | `cvpad`, `gamepad.h` |
| `usb-keyboard-mouse` | a USB keyboard and mouse, using TinyUSB directly | TinyUSB host, with `usbinput.c` in the example |
| `usb-midi-out` | USB as output, with the cart as a MIDI device on a computer, using TinyUSB directly | TinyUSB device, with `usbmidi_dev.c` in the example |
| `sd-card` | listing a folder, reading a file, and making, writing and reading back a save file | `cvsd`, `fsro.h` |
| `open-file` | declaring an extension (`.txt`) and getting the file from the menu | `cvlaunch.h`, `cvsd_open_launched` |
| `frames` | a whole picture every TV frame, as command lists with a time budget | `cvframes`, `cvdiag` |
| `samples` | sound as samples through the sound chip used as a DAC, with lists that run back to back | `cvframes`, `tools/mkdac3.py` |
| `two-controllers` | both of the console's controllers and USB pad 1's two sticks, on the frame kernel | `cvf_input2`, `cvusb` |
| `psram` | the 8 MB PSRAM | `cvpsram` |

```sh
cmake -S examples -B build-examples -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build-examples            # build-examples/<folder>/EX....CVP
test/run.sh                        # all of them on the Mac (docs/testing.md)
```

The SDK's `cvusb` does what the three USB examples do, and MIDI in as
well. You turn each feature on with one word in CMake
(`docs/features.md`), and `tools/new-plugin.py` can write a plugin with
any of them working. The keyboard and MIDI examples call TinyUSB
directly, for a plugin that needs that. The game pad example uses only
`cvpad`, the SDK's game pad library.

To start your plugin from an example, copy its files over `src/`, and
copy its `CMakeLists.txt` lines into the top `CMakeLists.txt` in place of
the ones there (`main.c` becomes `src/main.c`, and so on). Then change
the ID.

## Two kinds of plugin

Plugins on the screen kernel (`parrotcart_screen()`, used by every
example except `frames`, `samples` and `two-controllers`) draw into a canvas whenever they like, and the
SDK sends the console what changed, one list at a time. Sound is the
chip's 10 bytes, sent when they change, and the console's controller
comes in as reports. The screen kernel is a good fit for tools, menus,
viewers, players, and turn-based or slow-moving games.

Plugins on the frame kernel (`parrotcart_frames()`, used by `frames`,
`samples` and `two-controllers`)
give the console one command list every TV frame, and choose exactly
which bytes of video memory and which sound-chip bytes go out in it. The
frame kernel is a good fit for emulators, video and fast games. It has
no canvas or font, so you encode the picture yourself (our NES plugin
has a full encoder).

Each plugin uses one kernel or the other, never both.

## What isn't in the SDK

- A call that sends a plugin back to the menu by itself. The player goes
  back with RESET, or by holding both fire buttons and `*` (Select and
  Start on a USB pad) for 2 seconds (`cvhome.h`). The frame kernel has
  `cvf_menu_now()` for a plugin that can't continue, but it isn't a
  clean exit, because the console is left running the plugin's kernel.
- Sprites, scrolling or a text console. The screen kernel shows a bitmap,
  and the frame kernel shows whatever you send it.
- A way to ask the menu for a second file. A plugin gets one file when it
  starts, but it can list folders and open other files itself
  (`sd-card`).
