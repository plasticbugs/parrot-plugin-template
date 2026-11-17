# Screenshots

The registry shows up to three screenshots of a plugin, inside a picture
of a TV.

- **PNG, exactly 256x192 pixels**: the console's picture and nothing else.
  No border, no TV frame, no scaling (the registry draws the frame and
  scales it).
- **At most 100 KB each.** A real one is 1 to 10 KB.
- **Up to 3**, attached to the GitHub release as `screenshot-1.png`,
  `screenshot-2.png`, `screenshot-3.png`. The first is the one shown in
  lists.

Keep them in the repository's `screenshots/` folder under those names: the
release workflow checks them and attaches whatever is there.

```sh
python3 tools/png256.py check screenshots/*.png     # size and weight
```

## Taking them

**From the plugin's own code, on your computer.** `tools/screenshot.sh`
builds your plugin against the kit's model of the console
(`docs/testing.md`), plays it a script of inputs, and saves the picture
at the end, already 256x192:

```sh
tools/screenshot.sh screenshots/screenshot-1.png                      # src/, after 2 seconds
tools/screenshot.sh screenshots/screenshot-2.png src 4000 "500:key=3 1200:joy=up+fire"
```

The script is `MS:WHAT=VALUE` steps (`test/host/host.c` lists them all):
the stick and fire buttons, keypad keys, a USB pad and its sticks, a USB
keyboard and mouse. With `PC_CARD=image PC_OPEN=/path` the plugin is
started with a file from a disk image, as the menu would.

This works for a plugin whose `main.c` builds on its own with the SDK's
libraries, as the examples do. For one with a big core (an emulator),
write a small host program of your own that calls your drawing code, as
the cart's own plugins do.

**The same, through the real kernel in MAME.** `MAME=1
tools/screenshot.sh ...` sends the lists your plugin made to the SDK's
Z80 kernel running on MAME's ColecoVision, and crops MAME's snapshot. The
picture is the same; use it when you want to be sure. It needs `mame` and
the ColecoVision BIOS (`MAME_ROMPATH=dir`, holding `coleco.zip`).

**From any emulator's snapshot.** A MAME snapshot of the ColecoVision is
280x216: the picture with a 12-pixel border.

```sh
python3 tools/png256.py crop snapshot.png screenshots/screenshot-1.png
```

takes the middle 256x192 of a bigger PNG.

**From the console.** A capture card's picture is scaled and soft: fine
for showing off, not for the registry, which wants the exact pixels.

## MAME is always headless here

Every MAME run in this kit is the one in `tools/mame-replay.sh`:

```sh
SDL_VIDEODRIVER=dummy mame coleco -video none -videodriver dummy -window -nomaximize \
    -sound none -nothrottle -skip_gameinfo -seconds_to_run N \
    -snapshot_directory DIR -snapname NAME -autoboot_script script.lua ...
```

No window, never full screen, and a time limit so it can't hang. Pictures
come out as snapshots (`-snapname`, or `manager.machine.video:snapshot()`
in Lua), never by looking at a window. If you script MAME yourself for a
plugin, keep to that: a full-screen MAME takes over the display of
whoever is at the machine, every time a test runs.
