# What in this repository isn't the Parrot Cart contributors'

The template, its examples, tools and docs, and the copy of the cart's SDK
in `sdk/`, are MIT (`LICENSE`). These parts came from elsewhere and keep
their own terms:

| Where | What | Licence |
|---|---|---|
| `sdk/firmware/revc/sdk/tinyusb/` | [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach and contributors), with the cart's fixes, listed in its `VENDORED.md` | MIT (`sdk/firmware/revc/sdk/tinyusb/LICENSE`) |
| `sdk/firmware/revc/midihello/tinyusb_fix/hub.c` | TinyUSB 0.18's hub driver with one fix (used only when a plugin builds against the pico-sdk's TinyUSB) | MIT (its header) |
| `cmake/pico_sdk_import.cmake` | from the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD 3-Clause |

A built plugin also contains the Raspberry Pi Pico SDK's runtime (BSD
3-Clause) and, if it uses USB, TinyUSB (MIT). Say so in your plugin's
credits (`docs/release-checklist.md`).

The 8x8 screen font's letter shapes come from the 1982 arcade game Mr. Do!
