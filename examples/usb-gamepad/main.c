// USB game pad example: the cart's USB-C port as a host.
//
// cvpad reads up to two pads -- a Nintendo Switch Pro Controller (and pads
// that act as one, like 8BitDo's in Switch mode), or any plain HID pad --
// and gives each as a gamepad_t (gamepad.h): buttons named by where they
// are (GP_SOUTH is the bottom face button whatever its label), and sticks
// calibrated to -2048..2047 with x right and y up.
//
// The cart can't power a pad: the pad goes in a powered hub (or a dock),
// and the hub in the cart. cvpad_init() starts a 1 ms timer that does all
// the USB work, so the loop only asks for the state.
#include <stdio.h>

#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvpad.h"
#include "cvscreen.h"
#include "handoff.h"
#include "mailbox.h"            // REPORT_KIND_*
#include "rom_image.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static gamepad_t pad;
static bool have_pad;

static const struct { uint32_t bit; const char *name; } BUTTON[GP_BUTTONS] = {
    {GP_UP, "UP"}, {GP_DOWN, "DOWN"}, {GP_LEFT, "LEFT"}, {GP_RIGHT, "RIGHT"},
    {GP_SOUTH, "SOUTH"}, {GP_EAST, "EAST"}, {GP_WEST, "WEST"}, {GP_NORTH, "NORTH"},
    {GP_L, "L"}, {GP_R, "R"}, {GP_ZL, "ZL"}, {GP_ZR, "ZR"},
    {GP_MINUS, "MINUS"}, {GP_PLUS, "PLUS"}, {GP_HOME, "HOME"}, {GP_CAPTURE, "CAPT"},
    {GP_LSTICK, "L3"}, {GP_RSTICK, "R3"},
};

// a stick as a dot in a 64x64 square (on the 8-pixel grid, so the dot's
// white and the square's blue are the only two colours in any run)
static void stick(canvas_t *c, int x0, int y0, int x, int y) {
    canvas_fill(c, x0, y0, 64, 64, C_DBLUE);
    const int px = (x + 2048) * 60 / 4096, py = (2047 - y) * 60 / 4096;   // y up -> the screen's y down
    canvas_fill(c, x0 + px, y0 + py, 4, 4, C_WHITE);
}

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    char s[33];
    cvs_line(c, 8, 8, 240, "USB GAME PAD", C_LYELLOW, C_BLACK, 2);
    if (!have_pad) {
        canvas_fill(c, 0, 32, 256, 144, C_BLACK);
        cvs_line(c, 8, 32, 240, cvpad_count() ? "A PAD IS STARTING..." : "NO PAD YET", C_WHITE, C_BLACK, 1);
        cvs_line(c, 8, 48, 240, "PLUG A PAD INTO A POWERED HUB,", C_GRAY, C_BLACK, 1);
        cvs_line(c, 8, 60, 240, "AND THE HUB INTO THE CART", C_GRAY, C_BLACK, 1);
    } else {
        cvs_line(c, 8, 32, 240, cvpad_kind(0), C_LGREEN, C_BLACK, 1);
        // the buttons, 5 to a row, the ones held in white
        for (int i = 0; i < 20; i++)
            cvs_line(c, 8 + 48 * (i % 5), 44 + 10 * (i / 5), 48, i < GP_BUTTONS ? BUTTON[i].name : "",
                     i < GP_BUTTONS && pad.buttons & BUTTON[i].bit ? C_WHITE : C_DBLUE, C_BLACK, 1);
        stick(c, 32, 88, pad.lx, pad.ly);
        stick(c, 160, 88, pad.rx, pad.ry);
        snprintf(s, sizeof s, "%5d,%5d", pad.lx, pad.ly);
        cvs_line(c, 16, 154, 96, s, C_WHITE, C_BLACK, 1);
        snprintf(s, sizeof s, "%5d,%5d", pad.rx, pad.ry);
        cvs_line(c, 144, 154, 96, s, C_WHITE, C_BLACK, 1);
        // cvpad_dirs(): the d-pad, or the left stick past 40% of its way,
        // as four directions -- what a game that wants a joystick asks for
        const uint32_t d = cvpad_dirs(&pad);
        snprintf(s, sizeof s, "DIRECTIONS %s%s%s%s", d & GP_UP ? "UP " : "", d & GP_DOWN ? "DOWN " : "",
                 d & GP_LEFT ? "LEFT " : "", d & GP_RIGHT ? "RIGHT" : "");
        cvs_line(c, 8, 165, 240, s, C_LYELLOW, C_BLACK, 1);
    }
    cvs_line(c, 8, 180, 240, "RESET: BACK TO THE MENU", C_GRAY, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    cvpad_init();               // the USB port is a host from here on
    cvk_note("USB game pad example");

    uint32_t t_screen = 0;
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits))
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK)
                cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);

        if (t - t_screen >= 20000) {
            t_screen = t;
            // Pad 0 is the first one plugged in (player 1). A button tapped
            // and let go between two calls still shows in the second, so
            // ask once a frame, not more.
            have_pad = cvpad_get(0, &pad);
            const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
            if (n) cvk_list(list, n, screen.out_full, screen.seq);
        }
    }
}
