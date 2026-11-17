// usbmidi_dev.h -- the cart's USB port as a MIDI device, "Parrot Cart MIDI Example":
// with a computer on the port (its USB-C, the computer the host), the
// plugin.s MIDI goes to it, for a synth or a recorder there.
//
// usbmidi_start() makes the port a device; if the computer configures it
// within a second or so (usbmidi_mounted()), MIDI goes out; if not (a hub
// on the port, or nothing), usbmidi_stop() and the port can be a host.
// usbmidi_host_seen(): a host has answered (its frames, or a request),
// whether or not it has taken the device yet -- macOS asks "Allow
// accessory to connect?" the first time, and configures it once allowed.
//
// usbmidi_write() queues MIDI bytes (whole messages; a SysEx with its F0
// and F7); usbmidi_task() runs the USB stack and sends what's queued: call
// it often. A full queue drops the message, counted.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usbmidi_start(void);
bool usbmidi_mounted(void);
bool usbmidi_host_seen(void);
void usbmidi_stop(void);
void usbmidi_task(void);
bool usbmidi_write(const uint8_t *msg, uint32_t n);
uint32_t usbmidi_dropped(void);

#ifdef __cplusplus
}
#endif
