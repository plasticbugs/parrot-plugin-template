# Hello

Hello is the smallest plugin for Parrot Cart, the ColecoVision SD
cartridge. It puts text on the TV and shows what you're doing with the
console's controller.

This repository is also the Parrot Cart plugin template. Copy it and
change a few names to start your own plugin.

## Controls

- Stick, fire buttons and keypad: shown on the TV and counted as you
  press them
- RESET, or both fire buttons and \* held for 2 seconds: back to the cart's
  menu, which shows the count

## Credits

- Parrot Cart plugin template and SDK, by the Parrot Cart contributors,
  MIT: <https://github.com/plasticbugs/parrot-plugin-template>
- TinyUSB, by Ha Thach and contributors, MIT (in the SDK, but this
  plugin doesn't use USB): <https://github.com/hathach/tinyusb>
- Raspberry Pi Pico SDK, BSD 3-Clause:
  <https://github.com/raspberrypi/pico-sdk>

Source: put your repository's address here.

## License

MIT. See LICENSE. THIRD-PARTY.md lists the parts that came from other
projects.

## Making your own plugin

This file is also your plugin's README. The cart shows it on the
plugin's details page, 30 characters wide, and the plugins site shows it
too. Keep the parts above this heading in the same order: what the
plugin is, its controls, its credits with links to their source, and its
license. Then replace this section with anything else your users need to
know.

To start:

1. Copy the repository with **Use this template** on GitHub, and clone
   your copy.
2. Install the tools and build (docs/getting-started.md). You get
   `build/HELLO.CVP`.
3. Copy the `.CVP` into the `/PLUGINS` folder of the cart's SD card. It
   shows up in the cart's menu under Apps, on Home.
4. Run `tools/new-plugin.py`. It asks for the plugin's name, your name,
   the features you want (game pads, a keyboard, a mouse, MIDI in and
   out, the PSRAM, saves) and the file types it opens. Then it writes a
   plugin that builds, with each feature working on the TV
   (docs/features.md). To do it by hand
   instead, change the ID, the name and the developer in
   `CMakeLists.txt` to yours (docs/plugin-rules.md), and draw an icon
   (docs/icons.md).
5. Write your plugin in `src/`. `src/main.c` is the smallest plugin, and
   `examples/` has one for each thing a plugin can do:

| Example | Shows |
|---|---|
| screen | text and tile graphics |
| sound | the console's sound chip |
| usb-gamepad | a USB game pad, with its sticks |
| usb-keyboard-mouse | a USB keyboard and mouse |
| usb-midi-out | USB as output, sending MIDI to a computer |
| sd-card | listing a folder, reading a file, a save file |
| open-file | opening a file picked in the menu |
| frames | a full picture every TV frame |
| samples | sound as samples, with lists back to back |
| two-controllers | both of the console's controllers |
| psram | the 8 MB of PSRAM |

6. Try it on your computer first. `sim/run.sh src` runs your plugin in
   a window with sound, and you play it with the keyboard or a game pad
   (docs/simulator.md). `test/run.sh` runs the code of every example
   against a model of the console and takes its screenshots
   (docs/testing.md, docs/screenshots.md).
7. To release it, push a tag like `v1.0.0`. The workflow in
   `.github/workflows` builds the `.CVP` and attaches it to a GitHub
   release (docs/release-checklist.md). Then submit it to the plugin
   registry at <https://github.com/plasticbugs/parrot-cart-plugins>

You can also write a plugin in Lua instead of C. The Lua plugin in `lua/`
runs scripts from the card and has an editor for writing them on the
console, and a script can be built into a plugin of its own
(docs/lua.md).

If you use Claude Code, open this folder in it. The kit comes with a
skill that knows the SDK, the simulator and the release steps
(`.claude/skills/parrot-cart-plugin`), and Claude uses it when you ask
for help with your plugin. To use it in a project that isn't a copy of
this kit, copy that folder into `~/.claude/skills/`.

More help is in these docs:

- docs/getting-started.md: installing the tools, building, and using
  the card
- docs/hardware.md: the chips and memory, and what a plugin can use
- docs/features.md: game pads, keyboards, mice, MIDI, the PSRAM and the
  card, each turned on with one word in CMake
- docs/lua.md: writing a plugin in Lua, with every function a script
  can call
- docs/plugin-rules.md: IDs, versions, sizes, folders, and what a plugin
  can do
- docs/icons.md: the 8x8 icons
- docs/readme-spec.md: this file, and how the cart shows it
- docs/screenshots.md: screenshots for the plugins site
- docs/release-checklist.md: what to check before you tag a release
- docs/simulator.md: running your plugin in a window on your computer
- docs/testing.md: testing with and without a console
- sdk/firmware/revc/sdk/README.md: the SDK reference
