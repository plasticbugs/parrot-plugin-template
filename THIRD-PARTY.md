# What in this repository isn't the Parrot Cart contributors'

The template, its examples, tools and docs, and the copy of the cart's SDK
in `sdk/`, are MIT (`LICENSE`). These parts came from elsewhere and keep
their own terms:

| Where | What | License |
|---|---|---|
| `sdk/firmware/revc/sdk/tinyusb/` | [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach and contributors), with the cart's fixes, listed in its `VENDORED.md` | MIT (`sdk/firmware/revc/sdk/tinyusb/LICENSE`) |
| `sdk/firmware/revc/midihello/tinyusb_fix/hub.c` | TinyUSB 0.18's hub driver with one fix (used only when a plugin builds against the pico-sdk's TinyUSB) | MIT (its header) |
| `sim/third_party/rtmidi/` | [RtMidi](https://github.com/thestk/rtmidi) 6.1.0 (Gary P. Scavone and contributors), unchanged, listed in its `VENDORED.md`: the simulator's MIDI ports | MIT-style (`sim/third_party/rtmidi/LICENSE`) |
| `cmake/pico_sdk_import.cmake` | from the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD 3-Clause |
| `tools/licenses/GPL-2.0.txt`, `GPL-3.0.txt`, `LGPL-2.1.txt` | the GNU licenses, as the [Free Software Foundation](https://www.gnu.org/licenses/) publishes them | verbatim copies, as each text allows |
| `tools/licenses/Apache-2.0.txt` | the Apache License 2.0, as the [Apache Software Foundation](https://www.apache.org/licenses/) publishes it | verbatim copy |

A built plugin also contains the Raspberry Pi Pico SDK's runtime (BSD
3-Clause) and, if it uses USB, TinyUSB (MIT). Say so in your plugin's
credits (`docs/release-checklist.md`).

The 8x8 screen font's letter shapes come from the 1982 arcade game Mr. Do!
