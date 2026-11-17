# Features

A plugin turns on what it uses with one line of CMake. Each feature
brings its code, sets up the USB port with only what it needs, and adds
its flag to the `.CVP`'s `USES`. A plugin without a feature doesn't
carry its code.

```cmake
cvcart_features(myplugin PADS KEYBOARD MOUSE MIDI_IN MIDI_OUT PSRAM CARD)
```

It goes before `cvcart_plugin()`. `tools/new-plugin.py` writes it for
you, with each feature working on the TV in `src/main.c`:

```sh
tools/new-plugin.py                         # asks, a question at a time
tools/new-plugin.py --name "Star Sweeper" --by "Ann" \
    --features pads,keyboard,midi-in --screen text
```

| Feature | What plugs in | The USB port | `USES` |
|---|---|---|---|
| `PADS` | up to two game pads | host | usb-host |
| `KEYBOARD` | a USB keyboard | host | usb-host |
| `MOUSE` | a USB mouse | host | usb-host |
| `MIDI_IN` | a USB MIDI keyboard, or any class-compliant MIDI device | host | usb-host |
| `MIDI_OUT` | a computer: the cart is a MIDI device on it | device | usb-device |
| `PSRAM` | nothing: the cart's 8 MB PSRAM | | psram |
| `CARD` | nothing: the SD card, for files and saves | | |

`tools/new-plugin.py --name "Feature Test" --by You --features all`
makes a plugin with all of them on and every input on the TV: a quick way
to try a pad, a hub or a keyboard on the console.

## The USB port: host or device

The cart's USB port is one or the other.

- **A host**, for `PADS`, `KEYBOARD`, `MOUSE` and `MIDI_IN`. The cart
  can't power what is plugged in: use a powered USB hub between the
  devices and the cart. A hub takes several at once: two pads, a
  keyboard, a mouse and a MIDI keyboard together are fine.
- **A device**, for `MIDI_OUT`: the cart is a MIDI port on a computer,
  named after the plugin.

With host features and `MIDI_OUT` together, the port starts as a device.
A computer that takes the cart within 1.5 seconds keeps it a device.
Anything else (a hub, nothing at all) makes it a host. A Mac asks its
user before it takes a new MIDI device ("Allow accessory to connect?").
While it asks, the port waits for up to a minute, and `cvusb_mode()` says
`CVUSB_ASKING`: show that, and let a button call `cvusb_host_now()` to
stop waiting. The choice is made once, at the start.

MIDI goes both ways on whichever MIDI connection there is. As a device,
`cvusb_midi_read()` gets what the computer sends, and `cvusb_midi_send()`
sends to it. As a host with `MIDI_IN`, both go to the MIDI keyboard.

## The calls

All in `sdk/firmware/revc/sdk/cvusb.h`:

```c
#include "cvusb.h"

cvusb_start();                  // once, after cvk_start() or cvf_start()
for (;;) {
    cvusb_task();               // every pass of the loop: it never waits
    ...
}
```

A call for a feature that is off is a build error that names it: `turn
on KEYBOARD in cvcart_features()`.

### Game pads: `PADS`

```c
gamepad_t p;
if (cvusb_pad(0, &p)) {         // pad 0 or 1: false if it isn't there (yet)
    p.buttons                   // GP_SOUTH, GP_EAST, GP_UP, GP_L, GP_ZR ... held (gamepad.h)
    p.lx, p.ly, p.rx, p.ry      // the sticks, -2048..2047, y up
}
cvusb_pad_kind(0)               // "SWITCH PRO" or "HID PAD": what it is
```

Buttons are named by where they are: `GP_SOUTH` is the bottom face
button (B on a Nintendo pad, A on an Xbox one). The Switch Pro
Controller, pads in Switch mode and plain HID pads work. A Switch pad
takes a moment to start after it is plugged in: `cvusb_pad_kind()` names
it before `cvusb_pad()` has it. The triggers are buttons (`GP_ZL`,
`GP_ZR`).

### The keyboard: `KEYBOARD`

```c
cvkey_t k;
while (cvusb_key(&k)) {         // each key as it goes down and as it comes up
    k.usage                     // the key's USB code: 0x04 is A, 0x28 Enter (HID's usage table)
    k.ascii                     // its character on a US keyboard, 0 for none (the arrows, F1)
    k.mods                      // CVKEY_SHIFT, CVKEY_CTRL, CVKEY_ALT, CVKEY_GUI: held with it
    k.down                      // true: it went down
}
```

The modifiers come as keys too (usage 0xE0-0xE7). 32 keys wait at most.

### The mouse: `MOUSE`

```c
cvmouse_t m;
if (cvusb_mouse(&m)) {          // false: no mouse
    m.dx, m.dy                  // how far it moved since the last call, y down
    m.wheel                     // the same, for the wheel
    m.buttons                   // 1 left, 2 right, 4 middle
}
```

### MIDI: `MIDI_IN`, `MIDI_OUT`

```c
uint8_t m[3];
int n;
while ((n = cvusb_midi_read(m)) > 0) {      // n bytes: m[0] the status (0x90 note on ... | channel)
    ...
}
const uint8_t note_on[3] = {0x90, 60, 100};
cvusb_midi_send(note_on, 3);    // whole messages (a SysEx too); false: nowhere to send it, or no room now
```

Note, controller, program and pitch messages, the clock and the other
one-byte ones come in. SysEx doesn't. 64 messages wait at most;
`cvusb_midi_dropped()` counts what didn't fit.

### The port

```c
cvusb_mode()                    // CVUSB_HOST, CVUSB_DEVICE, CVUSB_WAITING, CVUSB_ASKING
cvusb_mode_name(cvusb_mode())   // "HOST", "ON A COMPUTER" ...: for the screen
cvusb_status_t s;
cvusb_status(&s);               // s.pads, s.keyboards, s.mice: how many; s.midi: a MIDI connection
```

As a host, the USB work runs every millisecond from a timer's interrupt,
so the pads keep being read while the plugin is busy. Don't call
TinyUSB's own functions (`tuh_*`, `tud_*`) in a plugin that uses these.

### Player 1: `cvinput.h`

The console's controller and USB pad 1 as one player, for either screen
kernel. With `PADS` on, a USB pad counts as the console's controller does
in the cart's menu: the d-pad or left stick is the stick, B and A the
fire buttons, X and Y the keypad's `*` and `#`. Without it, it is the
console's controller alone.

```c
cvin_t in;
cvin_init(&in);
cvin_report(&in, kind, bits);   // the screen kernel: each cvk_report()
cvin_frame(&in, joy, fire, key);    // the frame kernel: each frame's cvf_input()
cvin_update(&in);               // once a step
in.held                         // CVIN_UP, CVIN_FIRE_L ...: down now
in.key                          // 0-9, CVIN_STAR, CVIN_HASH, or -1
cvin_pressed(&in), cvin_key(&in)    // what went down since you last asked
```

### The PSRAM: `PSRAM`

```c
if (cvpsram_init()) {           // once, at the start
    CVPSRAM_CACHED[i]           // a uint8_t pointer to all 8 MB
}
```

`examples/psram` has more.

### The card: `CARD`

`cvsd.h`: the card, its FAT or exFAT file system, reading files, and
writing a file in place. `examples/sd-card` lists a folder and keeps a
save file; `examples/open-file` reads the file the menu opened the plugin
with. A plugin made by `tools/new-plugin.py` keeps its save in its home
folder (`HOME`), or in `/PLUGINS` beside the plugin if it has none. The
file system can't make folders: the user makes the home folder.

## What it costs

A plugin runs from the cart's RAM, about 500 KB, so its code counts as
RAM too. Over a plugin with no features (GCC 9.2.1):

| Features | Code | Data |
|---|---|---|
| `PADS` | 41.7K | 4.8K |
| `KEYBOARD` | 32.5K | 3.8K |
| `MOUSE` | 30.2K | 3.7K |
| `MIDI_IN` | 28.9K | 3.4K |
| `MIDI_OUT` | 14.1K | 7.1K |
| `PADS MIDI_OUT` | 53.8K | 12.0K |
| all five USB features | 60.8K | 13.2K |

Most of it is the USB host, which the host features share: each one
after the first adds a few K. `PSRAM` and `CARD` cost what the plugin
calls.

## Trying it on a Mac

`test/run.sh` builds the plugin's own code for the Mac and runs it against
a model of the console and the USB port. It reads `cvcart_features()`
from `CMakeLists.txt`, and its script can plug things in
(docs/testing.md):

| Script | What happens |
|---|---|
| `pad=south+left`, `lstick=1500,-900`, `pad=off` | a game pad |
| `keydown=04`, `keyup=04`, `mods=02` | a keyboard, by USB code |
| `mouse=40,20,1,1` | a mouse: dx, dy, buttons, wheel |
| `midiin=90,3C,64` | MIDI that comes in |
| `computer=0`, `computer=ask` | no computer on the port, or one that asks its user |

What the model can't check is the cart: real devices, a real hub, a real
computer. Try those on a console.

## Not here yet

- Keyboards and mice in their report protocol: they are read in their
  simpler boot protocol, so a keyboard's media keys and a mouse's extra
  buttons aren't there. A keyboard layout other than US: `usage` is
  right, `ascii` is the US key's.
- MIDI on more than one cable (port) of a device, or from more than one
  MIDI device at a time.
- The port changing its mind after the start (a computer plugged in
  later).
- A DualShock 4's right stick. A pad's right stick is read from its Rx
  and Ry when it has them, else its Z and Rz. A DualShock 4 has both, with
  its triggers on Rx and Ry, so its right stick reads its triggers.
