// usbinput.c -- a USB keyboard and mouse for a plugin (see usbinput.h).
//
// This file is the example's own, not the SDK's: copy it into your plugin.
// It comes from the cart's SCUMM plugin. TinyUSB's HID host driver does the
// work; each keyboard and mouse is asked for its "boot protocol" reports,
// which are the same for every make.
#include "usbinput.h"

#include <string.h>

#include "tusb.h"

static struct {
    int dx, dy;
    uint8_t buttons;
    uint8_t mice;                    // mouse interfaces mounted
} mouse;
static uint8_t keyboards;

// key changes, oldest first
#define KEYQ 32
static struct {
    uint8_t usage, mods;
    bool down;
} keyq[KEYQ];
static uint8_t kq_head, kq_tail;
static uint8_t last_keys[6], last_mods;

static void key_event(uint8_t usage, uint8_t mods, bool down) {
    uint8_t next = (uint8_t)((kq_tail + 1) % KEYQ);
    if (next == kq_head) return;     // full: dropped
    keyq[kq_tail].usage = usage;
    keyq[kq_tail].mods = mods;
    keyq[kq_tail].down = down;
    kq_tail = next;
}

static bool has(const uint8_t *keys, uint8_t k) {
    for (int i = 0; i < 6; i++)
        if (keys[i] == k) return true;
    return false;
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, uint8_t const *desc, uint16_t len) {
    (void)desc;
    (void)len;
    uint8_t proto = tuh_hid_interface_protocol(daddr, idx);
    if (proto != HID_ITF_PROTOCOL_KEYBOARD && proto != HID_ITF_PROTOCOL_MOUSE) return;
    if (proto == HID_ITF_PROTOCOL_MOUSE) mouse.mice++;
    else keyboards++;
    tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx) {
    uint8_t proto = tuh_hid_interface_protocol(daddr, idx);
    if (proto == HID_ITF_PROTOCOL_MOUSE && mouse.mice) {
        mouse.mice--;
        mouse.buttons = 0;
    }
    if (proto == HID_ITF_PROTOCOL_KEYBOARD) {
        if (keyboards) keyboards--;
        // whatever was held is let go
        for (int i = 0; i < 6; i++)
            if (last_keys[i]) key_event(last_keys[i], 0, false);
        memset(last_keys, 0, sizeof last_keys);
        last_mods = 0;
    }
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, uint8_t const *r, uint16_t len) {
    uint8_t proto = tuh_hid_interface_protocol(daddr, idx);
    if (proto == HID_ITF_PROTOCOL_MOUSE && len >= 3) {
        // boot report: buttons, x, y (signed)
        mouse.buttons = r[0] & 7;
        mouse.dx += (int8_t)r[1];
        mouse.dy += (int8_t)r[2];
    } else if (proto == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
        // boot report: modifiers, reserved, six keys (1-3: the keyboard's errors)
        const uint8_t mods = r[0], *keys = r + 2;
        if (keys[0] > 3) {
            for (int i = 0; i < 6; i++)
                if (last_keys[i] && !has(keys, last_keys[i])) key_event(last_keys[i], mods, false);
            for (int i = 0; i < 6; i++)
                if (keys[i] > 3 && !has(last_keys, keys[i])) key_event(keys[i], mods, true);
            memcpy(last_keys, keys, 6);
        } else if (keys[0] == 0) {
            for (int i = 0; i < 6; i++)
                if (last_keys[i]) key_event(last_keys[i], mods, false);
            memset(last_keys, 0, 6);
        }
        last_mods = mods;
    }
    tuh_hid_receive_report(daddr, idx);
}

void usbin_init(void) {
    tuh_hid_set_default_protocol(HID_PROTOCOL_BOOT);
    tusb_rhport_init_t host = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
    tusb_init(0, &host);
}

void usbin_task(void) {
    if (tuh_inited()) tuh_task();
}

bool usbin_mouse(int *dx, int *dy, uint8_t *buttons) {
    *dx = mouse.dx;
    *dy = mouse.dy;
    *buttons = mouse.buttons;
    mouse.dx = mouse.dy = 0;
    return mouse.mice != 0;
}

bool usbin_key(uint8_t *usage, uint8_t *mods, bool *down) {
    if (kq_head == kq_tail) return false;
    *usage = keyq[kq_head].usage;
    *mods = keyq[kq_head].mods;
    *down = keyq[kq_head].down;
    kq_head = (uint8_t)((kq_head + 1) % KEYQ);
    return true;
}

void usbin_count(int *kb, int *mice) {
    *kb = keyboards;
    *mice = mouse.mice;
}
