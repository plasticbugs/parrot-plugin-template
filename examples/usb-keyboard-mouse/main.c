// USB keyboard and mouse example: the cart's USB-C port as a host.
//
// The SDK reads game pads (see ../usb-gamepad); keyboards and mice are this
// example's own usbinput.c, 120 lines over TinyUSB's HID host driver that
// you copy into your plugin. Like a pad, they go in a powered hub.
//
//   usbin_init()      once: the port becomes a host
//   usbin_task()      every pass of the loop: TinyUSB's work (it never waits)
//   usbin_key()       the next key pressed or let go, by its HID usage
//   usbin_mouse()     the mouse's movement since the last call, its buttons
//
// Type, and the letters appear; move the mouse, and the pointer follows.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvscreen.h"
#include "handoff.h"
#include "mailbox.h"            // REPORT_KIND_*
#include "rom_image.h"
#include "usbinput.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];

#define COLS 30
static char typed[3][COLS + 1];             // what was typed: three lines, the last being written
static int mouse_x = 120, mouse_y = 28;     // in the mouse's field (240x64)
static uint8_t mouse_buttons;
static bool have_mouse;

// A key's HID usage (the USB standard's numbering, the same on every
// keyboard) as a character; 0 for the keys that aren't one. (The cart's
// font has A-Z, 0-9 and ! # & ' ( ) * + , - . / : ?; the rest show as '?'.)
static char key_char(uint8_t usage, bool shift) {
    static const char DIGIT[] = "1234567890", SHIFTED[] = "!@#$%^&*()";
    if (usage >= 0x04 && usage <= 0x1D) return (char)('A' + usage - 0x04);
    if (usage >= 0x1E && usage <= 0x27) return shift ? SHIFTED[usage - 0x1E] : DIGIT[usage - 0x1E];
    switch (usage) {
    case 0x2C: return ' ';
    case 0x2D: return shift ? '_' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default:   return 0;
    }
}

static void type(uint8_t usage, uint8_t mods) {
    char *line = typed[2];
    const size_t n = strlen(line);
    if (usage == 0x28) {                    // Enter: the lines move up
        memmove(typed[0], typed[1], sizeof typed[0] * 2);
        line[0] = 0;
    } else if (usage == 0x2A) {             // Backspace
        if (n) line[n - 1] = 0;
    } else {
        const char ch = key_char(usage, mods & 0x22);      // either Shift
        if (ch && n < COLS) { line[n] = ch; line[n + 1] = 0; }
    }
}

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    int keyboards, mice;
    char s[33];
    usbin_count(&keyboards, &mice);
    cvs_line(c, 8, 8, 240, "USB KEYBOARD, MOUSE", C_LYELLOW, C_BLACK, 1);
    snprintf(s, sizeof s, "KEYBOARDS %d   MICE %d", keyboards, mice);
    cvs_line(c, 8, 24, 240, s, keyboards || mice ? C_LGREEN : C_GRAY, C_BLACK, 1);
    for (int i = 0; i < 3; i++) cvs_line(c, 8, 44 + 10 * i, 240, typed[i], C_WHITE, C_BLACK, 1);
    canvas_fill(c, 8 + 8 * (int)strlen(typed[2]), 72, 8, 2, C_WHITE);          // the cursor, under the next place
    // the mouse's field, on the 8-pixel grid: the pointer's colour and the
    // field's are the only two in any run
    canvas_fill(c, 8, 88, 240, 72, C_DBLUE);
    if (have_mouse) canvas_fill(c, 8 + mouse_x, 88 + mouse_y, 8, 8, mouse_buttons & 1 ? C_LRED : mouse_buttons & 2 ? C_LGREEN : C_WHITE);
    cvs_line(c, 8, 176, 240, "RESET: BACK TO THE MENU", C_GRAY, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    usbin_init();
    cvk_note("USB keyboard example");

    uint32_t t_screen = 0;
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        usbin_task();
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits))
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK)
                cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);

        uint8_t usage, mods;
        bool down;
        while (usbin_key(&usage, &mods, &down))
            if (down) type(usage, mods);

        if (t - t_screen >= 20000) {
            t_screen = t;
            int dx, dy;
            have_mouse = usbin_mouse(&dx, &dy, &mouse_buttons);
            mouse_x += dx / 2;              // a mouse's counts are finer than the TV's pixels
            mouse_y += dy / 2;
            mouse_x = mouse_x < 0 ? 0 : mouse_x > 232 ? 232 : mouse_x;
            mouse_y = mouse_y < 0 ? 0 : mouse_y > 64 ? 64 : mouse_y;
            const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
            if (n) cvk_list(list, n, screen.out_full, screen.seq);
        }
    }
}
