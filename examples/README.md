# Examples

One small plugin for each thing a plugin can do. Each is a `main.c` to
read from the top, an icon, a README and five lines of CMake.

| Folder | Shows | SDK |
|---|---|---|
| `../src` | the smallest plugin: text, the console's controller, a note for the menu | `cvkernel`, `cvscreen`, `lib/wired.h` |
| `screen` | text at two sizes, the 15 colours, a map of 8x8 tiles, something moving | `cvscreen`, `vdp.h` |
| `sound` | the sound chip: notes on a voice, a drum on the noise | `cvk_sound`, `cvk_mb` |
| `usb-gamepad` | a USB pad: buttons, calibrated sticks, directions | `cvpad`, `gamepad.h` |
| `usb-keyboard-mouse` | a USB keyboard and mouse | TinyUSB host; the example's own `usbinput.c` |
| `usb-midi-out` | USB as output: the cart as a MIDI device on a computer | TinyUSB device; the example's own `usbmidi_dev.c` |
| `sd-card` | list a folder, read a file, make a save file, write it, read it back | `cvsd`, `fsro.h` |
| `open-file` | declare an extension (`.txt`), get the file from the menu | `cvlaunch.h`, `cvsd_open_launched` |
| `frames` | a whole picture every TV frame, as command lists with a time budget | `cvframes`, `cvdiag` |
| `psram` | the 8 MB PSRAM | `cvpsram` |

```sh
cmake -S examples -B build-examples -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build-examples            # build-examples/<folder>/EX....CVP
test/run.sh                        # all of them on the Mac (docs/testing.md)
```

To start your plugin from one: copy its files over `src/`, and its
`CMakeLists.txt` lines into the top `CMakeLists.txt` in place of the ones
there (`main.c` becomes `src/main.c`, and so on). Then change the ID.

## Two kinds of plugin

**On the screen kernel** (`parrotcart_screen()`; every example but
`frames`): the plugin draws into a canvas when it likes, and the SDK sends
the console what changed, a list at a time. Sound is the chip's 10 bytes,
sent when they change. The console's controller arrives as reports. Right
for tools, menus, viewers, players, turn-based and slow-moving games.

**On the frame kernel** (`parrotcart_frames()`; `frames`): the plugin hands
the console one command list every TV frame, and decides exactly which
bytes of video memory and which sound-chip bytes go out in it. Right for
emulators, video and fast games. There is no canvas or font here: the
picture is yours to encode (the cart's NES plugin has a full encoder).

A plugin uses one kernel or the other, never both.

## What isn't in the SDK

- **Going back to the menu from the plugin's own code.** RESET on the
  console is the way back (or switching the console off). The frame
  kernel has `cvf_menu_now()` for a plugin that can't go on; it isn't a
  clean exit, as the console is left running the plugin's kernel.
- **A USB keyboard, mouse or MIDI.** The SDK reads game pads. The two USB
  examples carry their own TinyUSB code, short enough to copy.
- **Sprites, scrolling, a text console.** The screen kernel shows a
  bitmap; the frame kernel shows what you send it.
- **Asking the menu for a second file.** A plugin gets one file, at its
  start; it can list folders and open others itself (`sd-card`).
