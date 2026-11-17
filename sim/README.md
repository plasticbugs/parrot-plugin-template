# sim/: the plugin simulator

How it is built, for whoever works on it. How to use it is in
`docs/simulator.md`.

## The idea

`test/host/host.c` already models everything a plugin talks to: the two
console kernels and their rules, the card through the SDK's file system,
the menu's launch block, the SDK's USB (`cvusb`), the PSRAM. `test/run.sh` drives those models
with a script and keeps the last picture. The simulator builds the same
`host.c`, unchanged, and drives it from a window instead: the picture on
the screen each TV frame, the sound chip's bytes to the speakers, the
keyboard, mouse and game pads as the controllers and USB devices, the
desktop's MIDI ports as the cart's USB MIDI.

So there is one set of models. A rule added to `host.c` is checked in both
places, and `sim/test.sh` checks that the simulator's pictures and logs
equal `test/run.sh`'s.

## The parts

| File | What it does |
|---|---|
| `main.c` | options, SDL, the window, each TV frame, RESET, the end |
| `model.c` | `#include "../test/host/host.c"`, with its hooks wired up, and the few functions the rest calls into it |
| `audio.c`, `psg.c` | the PSG bytes, stamped with the console time they reach the chip; an SN76489A model; SDL audio and the WAV |
| `input.c` | keyboard, mouse and game pads, to the console's controller, USB pads 1 and 2, a USB keyboard and mouse; the F2 role; the script's `sim*` words, made-up devices for headless tests |
| `midi.c` | the desktop's MIDI ports, through RtMidi's C API: in to the model, out from the `SIM_MIDI` hook, and `--midi-log` |
| `third_party/rtmidi/` | RtMidi 6.1.0, unchanged (its `VENDORED.md` has the checksums), built as a static library |
| `card.c` | the SD card: a FAT32 volume made from a folder, or a disk image |
| `extra.c` | SDK calls `host.c` doesn't answer (its examples don't make them) but real plugins do |
| `cxxmain.cpp` | for a plugin whose `main()` is C++ |
| `include/` | stand-ins for a few pico-sdk and TinyUSB headers that `test/host/include` doesn't have |
| `compat/` | what MSVC lacks (`unistd.h`), and a stored-only zlib for a machine without one |
| `CMakeLists.txt` | the build, for one plugin (`PLUGIN_DIR`), with the USB features its own `CMakeLists.txt` turns on (`test/host/plugin.cmake` reads them) |
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

`host.c` has seven macros, empty unless `SIM_HOOKS` is defined (model.c
defines them):

| Hook | Where | The simulator's |
|---|---|---|
| `SIM_FRAME()` | after each TV frame and its list | draw, sound, input, pace |
| `SIM_PSG(bytes, n, t)` | the frame kernel writes `n` PSG bytes, `t` Z80 cycles into the frame | to the audio queue, at their cycle |
| `SIM_MAILBOX()` | the plugin publishes the screen kernel's mailbox | its sound bytes, if the sound changed |
| `SIM_FAIL()` | after `fail()` has said which rule broke | the red mark, the title, `--strict` |
| `SIM_END()` | the end of the session, before the exit | the card's sync, the WAV, RESET's screen |
| `SIM_MIDI(bytes, n, to_dev)` | the plugin sends MIDI: to a computer, or (`to_dev`) to the MIDI device on the hub | `midi_send()`: the out port and `--midi-log` |
| `SIM_EVENT(what, v)` | a script word `host.c` doesn't know | the `sim*` words, to `input_script()`; false for any other |

## What changed in test/ for the simulator

For merging with other work on `test/host`:

- `test/host/tms9918.c` and `.h` (new): the TV picture from VRAM and the
  registers, in every mode the TMS9918A has, with sprites and their rules.
  `host.c` now draws both kernels' pictures with it (it had its own
  Graphics II drawing for the frame kernel). `test/vdp/main.c` and the
  `vdp` block in `test/run.sh` check it against MAME, mode by mode.
- `test/host/host.c`: the hooks above; the screen kernel's registers
  kept in `k_reg[8]` (a list's register writes were dropped before, so its
  picture assumed midihello.asm's); `end_us` is 64 bits, for "never".
- `test/host/build.sh`: builds `tms9918.c` too.
- `test/host/plugin.cmake` (new): reads a plugin's `CMakeLists.txt` (its
  ID, name and `cvcart_features()`) for `build.sh` and `sim/CMakeLists.txt`
  alike, so the models and the simulator always build a plugin with the
  same `CVUSB_*` switches.
- `sim/test.sh` has the same "the plugin in src/" and "the examples"
  markers as `test/run.sh`: `tools/new-plugin.py` writes a new plugin's
  tests into both, and leaves the examples out of both.
- `test/host/include/pico/stdlib.h`: `MIN` and `MAX`, and `extern "C"`
  for C++ plugins, as the pico-sdk's own header has.
- `test/run.sh`: the `vdp` checks; and `PC_SCRIPT` and the other `PC_*`
  settings no longer carry over from one run to the next (sh keeps a
  function's `VAR=value` prefixes set after the call).
- `test/host/host.c`'s USB model, for the simulator's phase 2: two pads
  (`pad2=`, `lstick2=`, `rstick2=`, `padkind=`, `pad2kind=`), each in its
  own slot as `cvpad.c` keeps them; `keydown=E0` to `E7` set the
  modifiers, as a keyboard's report has them; the key and MIDI queues are
  rings of the SDK's sizes (32 and 64). `usb_key()`, `usb_mouse()` and
  `usb_midi_bytes()` are what the script words and the simulator both
  call.
- `test/usb/` (new): a plugin with every USB feature on, which writes each
  input to its note. The `usb` block in `test/run.sh` and `sim/test.sh`
  runs it with `test/usb/host.script` and `device.script`, and
  `sim/test.sh` again with the `sim*` words.

## Phase 2: USB and MIDI

What `input.c` does with the desktop's devices, by role (F2):

- The keyboard in the "USB keyboard and mouse" role: each key down or up
  is `model_usb_key()`, which builds the keyboard's boot report and hands
  it to the SDK's `cvkeys_report()` (`cvusb_keys.c`), as the cart does.
  SDL's scancodes are HID usages, so nothing is translated. The mouse is
  in relative mode; its motion, buttons and wheel are `model_usb_mouse()`,
  sent as boot reports of at most 127 counts each.
- Game pads: two slots, kept as `cvpad.c` keeps them (a pad keeps the slot
  it took). `model_usb_pad()` sets each slot's state and kind every frame.
  The first pad in a slot is also the console's controller.
- Leaving the role, or losing the focus, lets go of every key and button.

What `midi.c` does: inputs are read once a TV frame and handed to
`model_midi_in()`, which packs them as USB-MIDI packets into the SDK's
queue. What the plugin sends comes through `SIM_MIDI` and goes out at
once, whole messages one at a time (running status written out). With
`MIDI_OUT` the port named after the plugin is a virtual one, both ways;
Windows has none, so it takes a named port. Headless, no port opens
unless one is named, and with one open the run is paced on the wall clock.

`--usb` sets the model's `computer` and `computer_asks` before the plugin
starts. Real MIDI inputs with `MIDI_IN` count as a MIDI device on the hub
(`model_midi_device()`), so the host side can send to it at once.

Headless tests can't press real keys, so the script's `sim*` words (the
`SIM_EVENT` hook) push SDL events for keys, mouse motion, buttons and the
wheel, and attach SDL virtual joysticks for pads. They go through
`input_poll()` as a window's would. Headless, the computer's own pads are
ignored (their drivers are off), and pad events are allowed without focus.

## Hook points for later

Things the simulator doesn't do yet, and where they would go:

- **Pad Test's view.** `include/tusb.h` is a port with nothing on it, so
  Pad Test runs and shows no device. `cvpad_info()` in `host.c` could
  describe the desktop pad.
- **A per-frame CPU warning.** The simulator can't time the cart's code.
  A rough one: count the plugin's SDK calls between frames, or time the
  host CPU and scale by a measured factor. Not done: it would mislead
  more than help.
- **Video and GIFs.** F12 saves one PNG. A GIF or video key would collect
  `model_picture()` each frame in `sim_on_frame()`.
- **MIDI timing.** MIDI out leaves when the plugin sends it, which is
  ahead of the audio queue's picture and sound by up to its length, and
  moves in frame-sized steps. Stamping messages with the console time and
  sending them on a thread, as `audio.c` does with the PSG, would fix both.
- **A computer plugged in later.** The model, like the cart, chooses the
  port's side once at the start. A key to plug a computer in or out would
  need `cvusb` to change its mind first.
