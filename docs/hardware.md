# The hardware

What a plugin runs on, and what it can use. The SDK takes care of the
wiring. The one part of it the SDK builds from, which GPIO carries each
console address and data line, is in `sdk/hw3/pins.py`.

## At a glance

| | |
|---|---|
| Chip | Raspberry Pi RP2350B, the chip in the Pico 2: two Arm Cortex-M33 cores at 150 MHz, with floating point and DSP instructions |
| RAM | 520 KB on the chip. A plugin gets all of it below `0x2007EC00`, about 507 KB for its code, data and heap |
| PSRAM | 8 MB (APS6404L), QPI at 75 MHz, in the address space through the chip's cache |
| Flash | 16 MB (W25Q128). A plugin can be up to 1 MB: the cart copies it into a 1 MB slot of its own and starts it from there |
| microSD | SPI at 25 MHz, about 1.1 MB/s reading. FAT16, FAT32 and exFAT |
| USB-C | Full speed (12 Mbit/s). As a host, through a powered hub: game pads, keyboards, mice and MIDI keyboards. As a device: plugged into a computer, for MIDI out, for example |
| LED | One red LED |
| The console | The ColecoVision's own Z80A at 3.58 MHz, its TMS9918A video chip with 16 KB of video RAM, its SN76489 sound chip, and its controllers |

## The two cores

Core 1 serves the cartridge port. Each time the console reads the
cartridge, core 1 answers inside the console's bus timing. The SDK starts it
for you. Leave it alone.

Core 0 is your plugin's. Call the SDK's task function (`cvk_task()` or
`cvf_task()`) often: it watches the console's power, and takes the console
back to the menu when RESET is pressed.

## Memory

- **SRAM.** A plugin runs from SRAM. The cart copies it in from flash when
  it starts, so code, data and heap share the 507 KB. The stacks are in the
  chip's two 4 KB scratch banks. `malloc` needs `HEAP` in `cvcart_plugin()`.
  Every K of code is a K off the heap: USB host support is 26 KB, and
  newlib's `printf` is 19 KB.
- **PSRAM.** 8 MB for big things: a game's ROM, buffers, pictures. Reads go
  through the cache, and there is an uncached alias for filling it fast
  (`cvpsram.h`). It is slower than SRAM, so keep tight loops and their data
  in SRAM.
- **Flash.** Your plugin's own 1 MB slot. A big plugin can run straight
  from flash instead of SRAM: the SCUMM plugin does.

## The console, as a plugin sees it

The cart doesn't replace the console's processor. The console's Z80 runs a
small program served by the cart (a kernel), and that program writes the
picture and the sound. The SDK has two:

- **The screen kernel** (`cvkernel` and `cvscreen`). You draw into a 256x192
  canvas on the cart, and what changed goes to the console. For menus, text,
  tools and music (PSG Synth, NES Info, the pad test).
- **The frame kernel** (`cvframes`). Each TV frame you hand the console one
  command list of video RAM writes and sound bytes. For emulators and
  anything that moves (the NES, Game Boy and Master System plugins). A list
  carries about 2 KB of video RAM writes a frame: about 650 bytes in the
  vertical blank, and about 1,350 more during the picture. The costs are in
  the header of `gbkernel.asm`.

What the console gives you:

- **Video.** TMS9918A, 256x192 pixels, 15 colours plus transparent, 16 KB of
  video RAM. In Graphics II mode each 8-pixel row of a tile has two colours.
  32 sprites, 4 on a line.
- **Sound.** SN76489: three square-wave tones and one noise channel, each
  with 16 volume steps. On a real console every sound write stalls the Z80
  for about 29 cycles. The kernels and their cost tables allow for it.
- **Controllers.** Joystick, two fire buttons and a 12-key keypad. The
  kernels read them on the console and report them to the cart
  (`cvf_input()` in the frame kernel).
- **Timing.** 60 TV frames a second (NTSC), 262 lines of 228 Z80 cycles
  each.

## USB

- **As a host:** `cvpad` reads up to two game pads (the Switch Pro
  Controller and plain HID pads). For keyboards, mice and MIDI, use the
  SDK's TinyUSB. The cart doesn't power USB devices, so they go through a
  powered hub.
- **As a device:** plugged into a computer, a plugin can be any USB device.
  The SCUMM plugin is a USB MIDI device, so its music plays on the
  computer's synth.

## What a plugin owns

While it runs, a plugin has all of SRAM below `0x2007EC00`, the PSRAM, USB
and the console. It must not write flash outside its own log area, and it
writes files on the card only in place (`fs_write_sec`). RESET, or turning the console off, goes back
to the menu, and a reboot always does too.

## What the cart doesn't have

No Wi-Fi or Bluetooth, no sound output of its own (sound goes through the
console's chip), and no clock that keeps time while it's off.
