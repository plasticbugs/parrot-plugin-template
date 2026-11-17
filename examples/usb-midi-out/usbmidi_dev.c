// usbmidi_dev.c -- the cart's USB port as a MIDI device (see usbmidi_dev.h).
//
// This file is the example's own, not the SDK's: copy it into your plugin.
// It comes from the cart's SCUMM plugin, which plays its music on a synth on
// the Mac this way. TinyUSB's device stack and its MIDI class: one cable,
// out to the computer (what the computer sends is read and dropped).
//
// The descriptors below are what the computer sees. Change the names, and
// before you share a plugin get a product ID of your own: 0xCAFE is
// TinyUSB's examples' vendor ID, fine for trying things at home
// (https://pid.codes gives IDs to open-source projects).
#include <string.h>

#include "hardware/structs/usb.h"
#include "tusb.h"

#include "usbmidi_dev.h"

#define QBYTES 16384                // queued MIDI bytes (a power of 2): room for a burst of SysEx

static uint8_t q[QBYTES];
static uint32_t q_head, q_tail;     // free-running: q_tail - q_head bytes queued
static uint32_t dropped;
static uint32_t sof0;               // the frame number at the start: a host's frames move it
static bool host_seen;

// ---- descriptors ---------------------------------------------------------------------

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,              // each interface says
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xCAFE,             // TinyUSB's examples' vendor
    .idProduct = 0x4C12,            // this example
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

enum { ITF_MIDI = 0, ITF_MIDI_STREAMING, ITF_TOTAL };
#define EP_OUT 0x01
#define EP_IN  0x81
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static const uint8_t desc_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_LEN, 0x00, 100),
    TUD_MIDI_DESCRIPTOR(ITF_MIDI, 4, EP_OUT, EP_IN, 64),
};

static const char *const strings[] = {
    NULL,                           // 0: the language, below
    "Parrot Cart",
    "Parrot Cart MIDI Example",
    "1",
    "Parrot Cart MIDI Example",
};

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&desc_device; }

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_config;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t d[32];
    unsigned n;
    if (index == 0) {
        d[1] = 0x0409;              // English (US)
        n = 1;
    } else {
        if (index >= sizeof strings / sizeof strings[0]) return NULL;
        const char *s = strings[index];
        n = (unsigned)strlen(s);
        if (n > 31) n = 31;
        for (unsigned i = 0; i < n; i++) d[1 + i] = (uint8_t)s[i];
    }
    d[0] = (uint16_t)(TUSB_DESC_STRING << 8 | (2 * n + 2));
    return d;
}

// ---- the port ------------------------------------------------------------------------

void usbmidi_start(void) {
    q_head = q_tail = 0;
    tusb_rhport_init_t dev = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &dev);
    sof0 = usb_hw->sof_rd & USB_SOF_RD_BITS;
    host_seen = false;
}

bool usbmidi_host_seen(void) {
    if (!host_seen && tud_inited() && (tud_connected() || (usb_hw->sof_rd & USB_SOF_RD_BITS) != sof0)) host_seen = true;
    return host_seen;
}

bool usbmidi_mounted(void) { return tud_inited() && tud_mounted(); }

void usbmidi_stop(void) {
    if (tud_inited()) tud_deinit(0);
}

uint32_t usbmidi_dropped(void) { return dropped; }

bool usbmidi_write(const uint8_t *msg, uint32_t n) {
    if (n == 0) return true;
    if (QBYTES - (q_tail - q_head) < n) {
        dropped++;
        return false;
    }
    for (uint32_t i = 0; i < n; i++) q[(q_tail + i) & (QBYTES - 1)] = msg[i];
    q_tail += n;
    return true;
}

void usbmidi_task(void) {
    if (!tud_inited()) return;
    tud_task();
    // what the computer sends: not wanted, but read so it doesn't back up
    uint8_t pkt[4];
    while (tud_midi_available()) tud_midi_packet_read(pkt);
    if (!tud_mounted()) return;
    // the queue into TinyUSB's FIFO: it packs the bytes into USB-MIDI packets
    // (a SysEx across several) and takes as many as it has room for
    while (q_tail != q_head) {
        uint32_t at = q_head & (QBYTES - 1), n = q_tail - q_head;
        if (n > QBYTES - at) n = QBYTES - at;
        uint32_t took = tud_midi_stream_write(0, q + at, n);
        if (!took) break;
        q_head += took;
    }
}
