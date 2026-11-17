# The plugin rules

What a plugin is to the cart's menu and to the plugin registry, and what
it may and may not do. Most of this is one line of CMake:

```cmake
cvcart_plugin(target ID developer.pluginname NAME "As The Menu Shows It" VERSION 1.2.3
              [BY "Developer"] [HOME /FOLDER] [OPENS .ext ...] [USES usb-host ...]
              [ICON icon.txt] [FILE_ICON icon.txt] [README README.md]
              [FILE NAME.CVP] [HEAP] [XIP])
```

## The ID: `developer.pluginname`

- Lower-case letters, digits, `.`, `-` and `_`; at least one dot; 32
  characters at most. `vexed.nes`, `jane-doe.chess`.
- **It is the plugin's identity, for good.** The menu and the registry go
  by it. Two files with the same ID are the same plugin: the menu uses
  the newer version and ignores the other. A new version with the same ID
  takes the old one's place in the cart's flash. Change the ID and it is a
  different plugin.
- The part before the first dot is you. Use the same one for all your
  plugins, and don't use someone else's. `parrotcart.` is the cart's own.
- The name (`NAME`, 32 characters at most) can change between versions.
  When two plugins have the same name, the menu shows the developer (`BY`,
  32 at most) beside it.

## Versions: `major.minor.patch`

- Three numbers, `1.0.3`. The menu compares them as numbers, so `1.10.0`
  is newer than `1.9.0`.
- Every release gets a new version, and its git tag is `v` + the version
  (`v1.0.3`). The release workflow refuses a tag that doesn't match the
  version in the `.CVP`.
- Before `1.0.0`, anything goes. After it: a new major version when saves
  or files from the old one stop working.

## The API version

The SDK stamps each `.CVP` with the plugin API it was built for (1 today).
A cart whose menu is older than a plugin's API refuses it, saying `NEEDS
NEWER CART FIRMWARE`; newer carts run older plugins. You don't set it:
it changes when you update `sdk/` (`tools/update-sdk.sh`) to an SDK with a
new API. Say in your release notes when a release needs a newer cart.

## Size: the 1 MB slot

The cart keeps each plugin in a 1 MB slot of its flash (the ten or so
used most recently). So:

- **The image is at most 1,044,480 bytes** (1 MB less the slot's 4K
  record). The `.CVP` is that plus a 512-byte header and the README.
- **An ordinary plugin runs from RAM**, where the cart's bus timing is
  safe: its code, data and variables together must fit below `0x2007EC00`,
  about 500 KB. The link fails with a clear message if they don't.
- **A plugin too big for RAM** (the SCUMM plugin is 900 KB) is built with
  `XIP` and runs from its flash slot. Its console-facing code must still
  be in RAM; `sdk/.../cvplugin.cmake` and `cvxip.h` say how.
- **Big data doesn't go in the plugin.** It goes on the card, and from
  there into the 8 MB PSRAM (`USES psram`).
- **No `malloc`** unless `cvcart_plugin(... HEAP)`. The build fails if it
  is linked without. With `HEAP`, call `cvheap_reserve()` first
  (`cvheap.h`).

## The home folder

`HOME /NES` names the folder on the card where the plugin's files live
(32 characters at most, starting with `/`). It is a convention the menu
and users share: games for a NES emulator go in `/NES`. Keep saves and
settings there too, or beside the file they belong to, not in the card's
top folder. A plugin doesn't need one.

## Extensions

`OPENS .nes .fds`: up to 8, each a dot and 1 to 7 letters or digits, any
case. The menu then opens those files with the plugin: A on such a file
starts the plugin with the file in its launch block (`examples/open-file`).

- When two plugins open the same extension, the menu asks which ("Open
  with"). So claim only what you really read.
- `.rom`, `.col`, `.bin` and movies are the menu's own, and `.gb`/`.gbc`
  its Game Boy mode's; a plugin may also claim `.gb` and be offered.
- `FILE_ICON` is the icon those files get in the menu (`docs/icons.md`).
- A plugin with no extensions is an app: it is started from the Plugins
  page.

## What it uses

`USES usb-host usb-device psram` is shown on the plugin's details page and
in the registry, so people know what to plug in. Say `usb-host` for pads,
keyboards and mice (they need a powered hub), `usb-device` when the cart
is plugged into a computer, `psram` when you use it.

## What a plugin owns while it runs

All of the chip's RAM below `0x2007EC00`, the PSRAM, the USB port (as a
host or as a device, one at a time), the card, and the console.

## What it must do

- **Start the console side first**: `cvk_start()` or `cvf_start()` at the
  top of `main()`. The console is parked, waiting for a cartridge.
- **Call `cvk_task()` (or `cvf_task()`) on every pass of its loop**, and
  keep each pass short. It watches the console's power and RESET, and
  takes the cart back to the menu. One long step at the start (the card's
  start is up to a second and a half) is fine; a loop that waits isn't.
- **Leave a note** when there is something to say: `cvk_note("Saved")`.
  32 characters at most, of which the menu shows 30.
- **Open the launched file by the launch block**
  (`cvsd_open_launched()`), not by its path.
- **Go easy on the card.** Read anything. Write only to files you made
  (`fs_create()`, at their final size) and to the launched file if your
  plugin edits it; never delete or overwrite other files.

## What it must not do

- Write to the cart's flash. The menu, the other plugins and the cart's
  settings live there. (A broken plugin can't brick the cart, whose USB
  recovery is in ROM, but it can force its owner to reinstall the menu.)
- Keep the console waiting: no `sleep_ms()` in the loop, no waiting for
  USB.
- Use both of the SDK's kernels, or call TinyUSB's host functions from the
  loop while `cvpad` is running (`cvpad.h`).

## Sharing a plugin

Plugins are free, separate downloads: nobody bundles them with a cart or
sells them. Every release has credits and a link to its source
(`docs/release-checklist.md`).

**Your plugin's licence is your choice.** The template and the SDK are MIT
(`LICENSE`, `THIRD-PARTY.md`), which lets you release your plugin under
any licence you like, the GPL included: an emulator core's licence often
decides it for you. Whatever you pick: the release is free, its README
has credits, and it links to the source. If your plugin contains GPL or
LGPL code, the source link is not a courtesy but that licence's
requirement, and it must be the source of exactly the version released.
