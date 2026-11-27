# What in this repository isn't the Parrot Cart contributors'

The template, its examples, tools and docs, and the copy of the cart's SDK
in `sdk/`, are MIT (`LICENSE`). These parts came from elsewhere and keep
their own terms:

| Where | What | License |
|---|---|---|
| `sdk/firmware/revc/sdk/tinyusb/` | [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach and contributors), with the cart's fixes, listed in its `VENDORED.md` | MIT (`sdk/firmware/revc/sdk/tinyusb/LICENSE`) |
| `sdk/firmware/revc/midihello/tinyusb_fix/hub.c` | TinyUSB 0.18's hub driver with one fix (used only when a plugin builds against the pico-sdk's TinyUSB) | MIT (its header) |
| `sim/third_party/rtmidi/` | [RtMidi](https://github.com/thestk/rtmidi) 6.1.0 (Gary P. Scavone and contributors), unchanged, listed in its `VENDORED.md`: the simulator's MIDI ports | MIT-style (`sim/third_party/rtmidi/LICENSE`) |
| `lua/vendor/lua-5.4.8/` | [Lua](https://www.lua.org) 5.4.8 (Roberto Ierusalimschy, Luiz Henrique de Figueiredo and Waldemar Celes, PUC-Rio), with two lines of `luaconf.h` changed, listed in its `VENDORED.md`: the Lua plugin's language | MIT (`lua/vendor/lua-5.4.8/LICENSE`) |
| `lib/z80/` | the Z80, from [MAME](https://github.com/mamedev/mame) 0.288's `src/devices/cpu/z80` (`z80.cpp`, `z80.h`, `z80.lst`; Juergen Buchmueller, Andrei I. Holub), in C | BSD 3-Clause (`lib/z80/LICENSE`) |
| `lib/sched/` | MAME 0.288's scheduler, from `src/emu/schedule.cpp` and `src/emu/diexec.cpp` (Aaron Giles), in C | BSD 3-Clause (`lib/sched/LICENSE`) |
| `lib/mcs48/` | the 8035 (MCS-48), from MAME 0.288's `src/devices/cpu/mcs48/mcs48.cpp` (Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud), in C | BSD 3-Clause (`lib/mcs48/LICENSE`) |
| `lib/sp0250/` | the SP0250 speech chip, from MAME 0.288's `src/devices/sound/sp0250.cpp` (Olivier Galibert), in C | BSD 3-Clause (`lib/sp0250/LICENSE`) |
| `lib/m6809/` | the 6809, from MAME 0.288's `src/devices/cpu/m6809` (`m6809.cpp`, `m6809.h`, `m6809inl.h`, `base6x09.lst`, `m6809.lst`; Nathan Woods), in C | BSD 3-Clause (`lib/m6809/LICENSE`) |
| `lib/m6800/` | the 6800, 6802 and 6808, from MAME 0.288's `src/devices/cpu/m6800` (`m6800.cpp`, `m6800.h`, `6800ops.hxx`; Aaron Giles), in C | BSD 3-Clause (`lib/m6800/LICENSE`) |
| `lib/nescv/` | the NES plugin's picture encoder, from [parrot-plugin-nes](https://github.com/plasticbugs/parrot-plugin-nes) (dfd06f5, the Parrot Cart contributors), with the Neo Geo Pocket Color plugin's additions (a band over some columns, `NESCV_FAST_KEYS`, pixels that are colours already) | LGPL 2.1 (`lib/nescv/LICENSE`) |
| `cmake/pico_sdk_import.cmake` | from the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD 3-Clause |
| `tools/licenses/GPL-2.0.txt`, `GPL-3.0.txt`, `LGPL-2.1.txt` | the GNU licenses, as the [Free Software Foundation](https://www.gnu.org/licenses/) publishes them | verbatim copies, as each text allows |
| `tools/licenses/Apache-2.0.txt` | the Apache License 2.0, as the [Apache Software Foundation](https://www.apache.org/licenses/) publishes it | verbatim copy |

A plugin built with one of `lib/`'s MAME cores contains MAME's code, so
credit MAME and the core's copyright holders in its README. A plugin
built with `lib/nescv/` contains LGPL code: credit it, and link to the
plugin's source as `docs/release-checklist.md` says. A built
plugin also contains the Raspberry Pi Pico SDK's runtime (BSD
3-Clause) and, if it uses USB, TinyUSB (MIT). Say so in your plugin's
credits (`docs/release-checklist.md`). A plugin written in Lua (`docs/lua.md`)
also contains Lua (MIT).

The 8x8 screen font's letter shapes come from the 1982 arcade game Mr. Do!
