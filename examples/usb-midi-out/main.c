// USB output example: the cart's USB-C port as a device, sending MIDI to a
// computer.
//
// Plug the cart's USB-C port into a Mac or PC: it appears as a MIDI input
// called "Parrot Cart MIDI Example", and the keypad's 1-8 play a scale on
// whatever is listening (GarageBand, a DAW, a MIDI monitor). MIDI is a good
// way out of the cart: every computer takes it with no driver, and a
// plugin can send anything as SysEx bytes, not only notes.
//
// The port is a device or a host, never both at once: a plugin picks at its
// start (the SCUMM plugin tries the device first, and if no computer
// answers within a second becomes a host for a keyboard and mouse).
//
// The device's descriptors and queue are this example's own usbmidi_dev.c,
// which you copy into your plugin:
//
//   usbmidi_start()     once: the port becomes a MIDI device
//   usbmidi_task()      every pass of the loop: TinyUSB's work, and sending
//   usbmidi_mounted()   a computer has taken the device
//   usbmidi_write()     queue whole MIDI messages
#include <stdio.h>

#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvscreen.h"
#include "handoff.h"
#include "rom_image.h"
#include "usbmidi_dev.h"
#include "wired.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static wired_t pad;
static const uint8_t SCALE[8] = {60, 62, 64, 65, 67, 69, 71, 72};     // MIDI's note numbers: C4 to C5
static int sounding = -1;       // the MIDI note that is on
static uint8_t last[3];         // the last message sent
static unsigned sent;

static void send(uint8_t status, uint8_t note, uint8_t velocity) {
    last[0] = status;
    last[1] = note;
    last[2] = velocity;
    if (usbmidi_write(last, 3)) sent++;
}

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    char s[33];
    cvs_line(c, 16, 8, 224, "USB MIDI OUT", C_LYELLOW, C_BLACK, 2);
    if (usbmidi_mounted()) cvs_line(c, 16, 40, 224, "A COMPUTER IS LISTENING", C_LGREEN, C_BLACK, 1);
    else if (usbmidi_host_seen()) cvs_line(c, 16, 40, 224, "A COMPUTER: WAITING FOR IT", C_LYELLOW, C_BLACK, 1);
    else cvs_line(c, 16, 40, 224, "PLUG THE CART INTO A COMPUTER", C_GRAY, C_BLACK, 1);
    cvs_line(c, 16, 64, 224, "KEYPAD 1-8  A NOTE", C_WHITE, C_BLACK, 1);
    for (int i = 0; i < 8; i++)
        canvas_fill(c, 16 + 28 * i, 84, 24, 16, sounding == SCALE[i] ? C_LGREEN : C_DBLUE);
    snprintf(s, sizeof s, "SENT %u", sent);
    cvs_line(c, 16, 116, 224, s, C_WHITE, C_BLACK, 1);
    if (sent) snprintf(s, sizeof s, "LAST %02X %02X %02X", last[0], last[1], last[2]);
    else s[0] = 0;
    cvs_line(c, 16, 128, 224, s, C_WHITE, C_BLACK, 1);
    cvs_line(c, 16, 176, 224, "RESET: BACK TO THE MENU", C_GRAY, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    wired_init(&pad);
    usbmidi_start();
    cvk_note("USB MIDI example");

    uint32_t t_screen = 0;
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        usbmidi_task();
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits)) {
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK)
                cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);
            else
                wired_report(&pad, kind, bits);
        }

        // the key held -> a note on, and a note off for the one before
        const int want = pad.key >= 1 && pad.key <= 8 ? SCALE[pad.key - 1] : -1;
        if (want != sounding) {
            if (sounding >= 0) send(0x80, (uint8_t)sounding, 0);       // note off, channel 1
            if (want >= 0) send(0x90, (uint8_t)want, 100);             // note on, channel 1
            sounding = want;
        }

        if (t - t_screen >= 20000) {
            t_screen = t;
            const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
            if (n) cvk_list(list, n, screen.out_full, screen.seq);
        }
    }
}
