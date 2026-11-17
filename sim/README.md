# sim/: the plugin simulator

How it is built, for whoever works on it. How to use it is in
`docs/simulator.md`.

## The idea

`test/host/host.c` already models everything a plugin talks to: the two
console kernels and their rules, the card through the SDK's file system,
the menu's launch block, USB, the PSRAM. `test/run.sh` drives those models
with a script and keeps the last picture. The simulator builds the same
`host.c`, unchanged, and drives it from a window instead: the picture on
the screen each TV frame, the sound chip's bytes to the speakers, the
keyboard and game pads as the controllers.

So there is one set of models. A rule added to `host.c` is checked in both
places, and `sim/test.sh` checks that the simulator's pictures and logs
equal `test/run.sh`'s.

## The parts

| File | What it does |
|---|---|
| `main.c` | options, SDL, the window, each TV frame, RESET, the end |
| `model.c` | `#include "../test/host/host.c"`, with its hooks wired up, and the few functions the rest calls into it |
| `audio.c`, `psg.c` | the PSG bytes, stamped with the console time they reach the chip; an SN76489A model; SDL audio and the WAV |
| `input.c` | keyboard, mouse and game pads, to the console's controller, a USB pad, a USB keyboard and mouse |
| `card.c` | the SD card: a FAT32 volume made from a folder, or a disk image |
| `extra.c` | SDK calls `host.c` doesn't answer (its examples don't make them) but real plugins do |
| `cxxmain.cpp` | for a plugin whose `main()` is C++ |
| `include/` | stand-ins for a few pico-sdk and TinyUSB headers that `test/host/include` doesn't have |
| `compat/` | what MSVC lacks (`unistd.h`), and a stored-only zlib for a machine without one |
| `CMakeLists.txt` | the build, for one plugin (`PLUGIN_DIR`) |
| `run.sh`, `run.bat` | build and run, in one command |
| `get-sdl.sh` | SDL3 into `build-sim/sdl3`, pinned by version and SHA-256 |
| `test.sh`, `wavcheck.py` | the simulator's checks, headless |

## How time works

The plugin runs on the main thread and calls the SDK as it would on the
cart. `host.c`'s clock moves on with each call the plugin makes
(`time_us_32()` advances it). Each time the clock passes a TV frame,
`host.c` runs that frame's list and calls `SIM_FRAME()`, which is
`sim_on_frame()` in `main.c`. There the simulator draws the frame, renders
the frame's sound, reads the input, and waits until the frame's time has
come on the desktop: on the audio device's queue when there is sound, on
the wall clock when not. Headless, it doesn't wait at all.

So the plugin sees 59.94 frames a second whatever the computer's speed,
and the sound never drifts from the picture. The cost: a plugin that never
calls the SDK never lets a frame pass. The cart's plugins all do.

## The hooks in test/host/host.c

`host.c` has five macros, empty unless `SIM_HOOKS` is defined (model.c
defines them):

| Hook | Where | The simulator's |
|---|---|---|
| `SIM_FRAME()` | after each TV frame and its list | draw, sound, input, pace |
| `SIM_PSG(bytes, n, t)` | the frame kernel writes `n` PSG bytes, `t` Z80 cycles into the frame | to the audio queue, at their cycle |
| `SIM_MAILBOX()` | the plugin publishes the screen kernel's mailbox | its sound bytes, if the sound changed |
| `SIM_FAIL()` | after `fail()` has said which rule broke | the red mark, the title, `--strict` |
| `SIM_END()` | the end of the session, before the exit | the card's sync, the WAV, RESET's screen |

## What changed in test/ for the simulator

For merging with other work on `test/host`:

- `test/host/tms9918.c` and `.h` (new): the TV picture from VRAM and the
  registers, in every mode the TMS9918A has, with sprites and their rules.
  `host.c` now draws both kernels' pictures with it (it had its own
  Graphics II drawing for the frame kernel). `test/vdp/main.c` and the
  `vdp` block in `test/run.sh` check it against MAME, mode by mode.
- `test/host/host.c`: the five hooks above; the screen kernel's registers
  kept in `k_reg[8]` (a list's register writes were dropped before, so its
  picture assumed midihello.asm's); `end_us` is 64 bits, for "never".
- `test/host/build.sh`: builds `tms9918.c` too.
- `test/host/include/pico/stdlib.h`: `MIN` and `MAX`, and `extern "C"`
  for C++ plugins, as the pico-sdk's own header has.
- `test/run.sh`: the `vdp` checks; and `PC_SCRIPT` and the other `PC_*`
  settings no longer carry over from one run to the next (sh keeps a
  function's `VAR=value` prefixes set after the call).

## Hook points for later

Things the simulator doesn't do yet, and where they would go:

- **USB MIDI in and out.** `host.c` models the usb-midi-out example's own
  device driver (`usbmidi_dev_*`), logging the bytes. For a MIDI port on
  the desktop: RtMidi (or SDL's future MIDI) in a new `midi.c`; out from
  where `host.c` logs `midi ...` bytes, in to a queue the model hands the
  plugin. Wait for the SDK's USB API from the `features` branch, and model
  that rather than an example's own driver.
- **A USB keyboard and mouse through the SDK.** Today they are `host.c`'s
  model of the usb-keyboard-mouse example's `usbinput.h`, fed by
  `model_usb_key()` and `model_usb_mouse()`. `input.c` already has the
  role (F2), the HID usages (SDL's scancodes are HID usages), relative
  mouse mode and buttons. When the SDK has a keyboard and mouse API,
  model it in `host.c` and point those two functions at it.
- **More than one USB pad.** `input.c` reads every pad; `host.c` models
  one (`cvpad_get(0)`). Widen `pad_state` to `CVPAD_MAX` and pass the
  pad's number through `model_usb_pad()`.
- **Pad Test's view.** `include/tusb.h` is a port with nothing on it, so
  Pad Test runs and shows no device. `cvpad_info()` in `host.c` could
  describe the desktop pad.
- **A per-frame CPU warning.** The simulator can't time the cart's code.
  A rough one: count the plugin's SDK calls between frames, or time the
  host CPU and scale by a measured factor. Not done: it would mislead
  more than help.
- **Video and GIFs.** F12 saves one PNG. A GIF or video key would collect
  `model_picture()` each frame in `sim_on_frame()`.
