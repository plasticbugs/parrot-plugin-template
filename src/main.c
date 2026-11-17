// The smallest useful Parrot Cart plugin: its name and icon in the menu,
// text on the TV, the console's controller, and a note left for the menu
// when RESET takes the console back to it.
//
// How a plugin runs. The cart has two cores:
//
//   core 1  is the cartridge: it answers every read the console makes, from
//           a small Z80 program (the SDK's screen kernel) that draws what
//           the plugin sends, plays its sound and reports the controller.
//           cvk_start() starts it; you never touch it again.
//   core 0  is this file: one loop that never blocks for long.
//
// The plugin doesn't write to the TV directly. It draws into a canvas
// (256x192, 15 colours), and the SDK sends the console what changed.
#include <stdio.h>

#include "pico/stdlib.h"

#include "cvkernel.h"           // the console: cvk_*
#include "cvscreen.h"           // the screen: cvs_*, and vdp.h's canvas_*
#include "handoff.h"            // made by the build: the ROM that parks the console between firmwares
#include "rom_image.h"          // made by the build: the screen kernel
#include "wired.h"              // the console's controller

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static wired_t pad;
static unsigned presses;

// The whole screen, every time: only what changed reaches the console, so
// there is nothing to gain by being clever here. Two colours at most in
// any 8x1 run of pixels (text on its background is always fine).
static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    char s[33];
    cvs_line(c, 16, 16, 224, "HELLO, PARROT CART", C_LYELLOW, C_BLACK, 1);
    cvs_line(c, 16, 32, 224, "MOVE THE STICK, PRESS A KEY", C_GRAY, C_BLACK, 1);

    snprintf(s, sizeof s, "STICK  %s%s%s%s", pad.held & WIRED_UP ? "UP " : "", pad.held & WIRED_DOWN ? "DOWN " : "",
             pad.held & WIRED_LEFT ? "LEFT " : "", pad.held & WIRED_RIGHT ? "RIGHT" : "");
    cvs_line(c, 16, 64, 224, s, C_WHITE, C_BLACK, 1);
    snprintf(s, sizeof s, "FIRE   %s%s", pad.held & WIRED_FIRE_L ? "LEFT " : "", pad.held & WIRED_FIRE_R ? "RIGHT" : "");
    cvs_line(c, 16, 80, 224, s, C_WHITE, C_BLACK, 1);
    if (pad.key < 0) snprintf(s, sizeof s, "KEYPAD");
    else snprintf(s, sizeof s, "KEYPAD %c", pad.key == WIRED_STAR ? '*' : pad.key == WIRED_HASH ? '#' : '0' + pad.key);
    cvs_line(c, 16, 96, 224, s, C_WHITE, C_BLACK, 1);
    snprintf(s, sizeof s, "PRESSES %u", presses);
    cvs_line(c, 16, 120, 224, s, C_LGREEN, C_BLACK, 2);

    cvs_line(c, 16, 176, 224, "RESET: BACK TO THE MENU", C_GRAY, C_BLACK, 1);
}

int main(void) {
    // First thing: the console is parked, waiting for a cartridge to appear.
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    wired_init(&pad);

    uint32_t t_screen = 0;
    for (;;) {
        const uint32_t t = time_us_32();
        // Every pass: is the console still on, has RESET been pressed? On
        // RESET this doesn't return: the cart goes back to the menu.
        cvk_task(t);

        // What the console says, once a TV frame: how far it got with the
        // screen (HELLO, ACK), and the controller.
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits)) {
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK)
                cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);
            else
                wired_report(&pad, kind, bits);
        }

        // The plugin's own work, in short steps.
        if (wired_pressed(&pad) | (wired_key(&pad) >= 0)) {
            presses++;
            static char note[33];
            snprintf(note, sizeof note, "Hello: %u presses", presses);
            cvk_note(note);     // the menu's bottom line when we go back (32 characters at most)
        }

        // The screen, 50 times a second at most.
        if (t - t_screen >= 20000) {
            t_screen = t;
            const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
            if (n) cvk_list(list, n, screen.out_full, screen.seq);
        }
    }
}
