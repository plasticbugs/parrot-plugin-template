# Two Controllers Example

Shows both of the console's controllers and USB pad 1 as you press them. It is a frame plugin that reads controller 2 with `cvf_input2()`.

For each controller you see its stick, its two fire buttons, its keypad and the three values the kernel sent. Below them are USB pad 1's two sticks, and what a twin-stick game would do with all of it. After RESET, the menu shows the values each controller sent last.

## Controls

- Both controllers: everything shows on the screen
- USB pad 1, through a powered hub: both sticks and the face buttons
- RESET: back to the menu

## Credits

- Parrot Cart plugin template and SDK, by the Parrot Cart contributors, MIT:
  <https://github.com/plasticbugs/parrot-plugin-template>
- TinyUSB, by Ha Thach and contributors, MIT:
  <https://github.com/hathach/tinyusb>
- Raspberry Pi Pico SDK, BSD 3-Clause:
  <https://github.com/raspberrypi/pico-sdk>
