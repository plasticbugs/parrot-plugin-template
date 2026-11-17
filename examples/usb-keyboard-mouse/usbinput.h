// usbinput: a USB keyboard and mouse through the cart's USB port (a dock or
// hub). TinyUSB host; each keyboard and
// mouse interface in its boot protocol, so any of them reports the same way.
//
//   usbin_init()   once (the USB port becomes a host)
//   usbin_task()   often: TinyUSB's work, never blocking
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usbin_init(void);
void usbin_task(void);

// the mice's movement since the last call, in counts, and their buttons
// now (bit 0 left, 1 right, 2 middle); false while no mouse is plugged in
bool usbin_mouse(int *dx, int *dy, uint8_t *buttons);

// the next key pressed or let go: its HID usage (0x04 'a' ...), the
// modifiers then (bit 0/4 ctrl, 1/5 shift, 2/6 alt), down or up; false
// when there's none
bool usbin_key(uint8_t *usage, uint8_t *mods, bool *down);

// keyboards and mice plugged in now
void usbin_count(int *keyboards, int *mice);

#ifdef __cplusplus
}
#endif
