# Hello

The smallest plugin for the Parrot Cart, the ColecoVision SD cartridge: it
puts text on the TV and shows what the console's controller is doing.

It is also the Parrot Cart plugin template. Copy this repository, change
a few names, and you have a plugin of your own that builds.

## Controls

- Stick, fire buttons, keypad: shown on the TV as you use them, and
  counted
- RESET: back to the cart's menu, which shows the count

## Credits

- Parrot Cart plugin template and SDK, by the Parrot Cart contributors,
  MIT: <https://github.com/plasticbugs/parrot-plugin-template>
- TinyUSB, by Ha Thach and contributors, MIT (in the SDK; this plugin
  doesn't use USB): <https://github.com/hathach/tinyusb>
- Raspberry Pi Pico SDK, BSD 3-Clause:
  <https://github.com/raspberrypi/pico-sdk>

Source: put your repository's address here.

## License

MIT: see LICENSE. THIRD-PARTY.md lists what came from elsewhere.

## Making your own plugin

This file is your plugin's README: the cart shows it, 30 characters wide,
on the plugin's details page, and the plugin registry shows it too. So
everything above this heading is the pattern to keep: what the plugin is,
its controls, its credits with links to their source, its license. Replace
this section with whatever else your users need.

To start:

1. Copy the repository ("Use this template" on GitHub) and clone it.
2. Install the tools and build: docs/getting-started.md. You get
   `build/HELLO.CVP`.
3. Copy the `.CVP` into the `/PLUGINS` folder of the cart's SD card. It is
   in the cart's menu under Plugins.
4. Run `tools/new-plugin.py`. It asks for the plugin's name, yours, and
   the features you want (game pads, a keyboard, a mouse, MIDI in and
   out, the PSRAM, saves), and writes a plugin that builds, each feature
   working on the TV (docs/features.md). Or by hand: in `CMakeLists.txt`,
   change the ID, the name and the developer to yours
   (docs/plugin-rules.md), and draw an icon (docs/icons.md).
5. Write your plugin in `src/`. `src/main.c` is the smallest one;
   `examples/` has one for each thing a plugin can do:

| Example | Shows |
|---|---|
| screen | text and tile graphics |
| sound | the console's sound chip |
| usb-gamepad | a USB game pad, with its sticks |
| usb-keyboard-mouse | a USB keyboard and mouse |
| usb-midi-out | USB as output: MIDI to a computer |
| sd-card | listing a folder, reading a file, a save file |
| open-file | opening a file picked in the menu |
| frames | a full picture every TV frame |
| psram | the 8 MB of PSRAM |

6. Try it on your computer first. `sim/run.sh src` runs your plugin in a
   window, with sound, played with the keyboard or a game pad
   (docs/simulator.md). `test/run.sh` runs every example's own code
   against a model of the console, and takes its screenshots
   (docs/testing.md, docs/screenshots.md).
7. Release it: tag `v1.0.0`, and the workflow in `.github/workflows`
   builds the `.CVP` and attaches it to a GitHub release
   (docs/release-checklist.md). Then tell the registry:
   <https://github.com/plasticbugs/parrot-cart-plugins>

The docs:

- docs/getting-started.md: tools, building, the card
- docs/hardware.md: the chips, the memory, and what a plugin can use
- docs/features.md: game pads, keyboards, mice, MIDI, the PSRAM and the
  card, each a switch in CMake
- docs/plugin-rules.md: IDs, versions, sizes, folders, what a plugin may do
- docs/icons.md: the 8x8 icons
- docs/readme-spec.md: this file, and how the cart shows it
- docs/screenshots.md: the registry's screenshots
- docs/release-checklist.md: before you tag
- docs/simulator.md: your plugin in a window on your computer
- docs/testing.md: testing without a console, and on one
- sdk/firmware/revc/sdk/README.md: the SDK's reference
