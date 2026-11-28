# lib

Code a plugin can build on, beyond the SDK's. `lib/` is on every
plugin's include path (`#include "z80/z80.h"`); add a folder's `.c` file
to the plugin's sources, or include it where its README says.

| Folder | What | License |
|---|---|---|
| `wired.h` | the console's controller for a screen-kernel plugin | MIT |
| `tms9918_rgb.h` | the console's 16 colours as RGB, for previews | MIT |
| `scalen/` | a picture scaled from N columns or lines to M, keeping thin strokes | MIT |
| `dial/` | an arcade dial or spinner from the stick, a pad or a mouse | MIT |
| `z80/` | the Z80 as MAME 0.288 runs it, one copy for any number of CPUs | BSD 3-Clause (MAME) |
| `z80lite/` | a small, fast Z80 that runs whole instructions, for a sound CPU or when RAM is short | MIT |
| `sched/` | MAME 0.288's scheduler: CPUs, timers and input lines in MAME's order | BSD 3-Clause (MAME) |
| `mcs48/` | the 8035 and its family (MCS-48), an arcade sound board's CPU | BSD 3-Clause (MAME) |
| `sp0250/` | the GI SP0250 speech chip | BSD 3-Clause (MAME) |
| `m6809/` | the 6809 | BSD 3-Clause (MAME) |
| `m6800/` | the 6800, 6802 and 6808 | BSD 3-Clause (MAME) |
| `nescv/` | a frame plugin's picture encoder: an emulator's picture as the frame kernel's lists, a band at a time | LGPL 2.1 (the NES plugin) |

The MAME cores came from the cart's arcade plugins (Time Pilot, Star
Trek, Donkey Kong Jr., Robotron). Each was checked by building its
plugin on it and comparing with MAME, frame by frame and instruction by
instruction, and each README says how. They are templates: the board's
`.c` file sets the CPU's connections (memory, ports, waits) as macros
and includes the core, so they run inline. Build that file with `-O3
-fno-data-sections` (`references/porting.md`, "Memory and the build").

`nescv/` came from the NES plugin, and the Neo Geo Pocket Color plugin
runs on it too. Its README says how it was checked.

`z80lite/` came from the Neo Geo Pocket Color plugin's sound CPU. It is
a third the size of `z80/`, but it runs whole instructions, so it can't
match MAME's timeslices. Its README says which of the two to use, and
how it was checked against `z80/`.

A plugin that uses one keeps its license with it: the folder's `LICENSE`
and a credit in the plugin's README.

`lib/test.sh` runs every folder's smoke test; the kit's CI runs it too.
