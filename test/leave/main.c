// test/leave: the screen kernel's exit function (cvk_on_exit), which runs
// on the way back to the menu. Not an example to copy: a plugin for
// test/run.sh and sim/test.sh.
//
// The exit function waits as long as the keypad last said, then leaves a
// note saying so: 1 is half a second, 2 is 5 seconds, longer than its
// CVK_EXIT_MS, so the watchdog reboots into the menu first (without the
// note). 3 sets no exit function. It also calls cvk_task() and
// cvk_report(), which must do nothing then.
#include <stdio.h>

#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvscreen.h"
#include "handoff.h"
#include "rom_image.h"
#include "wired.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static wired_t pad;
static uint32_t exit_ms = 500;
static int calls_after;                 // cvk_report() answers during the exit function (none)

static void on_exit(void) {
    const uint32_t t0 = time_us_32();
    while (time_us_32() - t0 < exit_ms * 1000) {
        cvk_task(time_us_32());         // nothing now, and no way back into the exit function
        int kind;
        uint8_t bits;
        if (cvk_report(time_us_32(), &kind, &bits)) calls_after++;
    }
    char s[64];                         // (cvk_note keeps the first 32)
    snprintf(s, sizeof s, "left after %lu ms%s", (unsigned long)exit_ms, calls_after ? ", reports!" : "");
    cvk_note(s);
}

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    char s[33];
    cvs_line(c, 16, 16, 224, "EXIT FUNCTION TEST", C_LYELLOW, C_BLACK, 1);
    if (exit_ms) snprintf(s, sizeof s, "ON THE WAY BACK: %lu MS", (unsigned long)exit_ms);
    else snprintf(s, sizeof s, "NO EXIT FUNCTION");
    cvs_line(c, 16, 40, 224, s, C_WHITE, C_BLACK, 1);
    cvs_line(c, 16, 56, 224, "1: 0.5 S  2: 5 S  3: NONE", C_GRAY, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    wired_init(&pad);
    cvk_on_exit(on_exit);
    cvk_note("left with no exit function");
    uint32_t t_step = 0;
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits)) {
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK) cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);
            else wired_report(&pad, kind, bits);
        }
        switch (wired_key(&pad)) {
        case 1: exit_ms = 500; cvk_on_exit(on_exit); break;
        case 2: exit_ms = 5000; cvk_on_exit(on_exit); break;
        case 3: exit_ms = 0; cvk_on_exit(NULL); break;
        default: break;
        }
        if (t - t_step < 20000) continue;
        t_step = t;
        const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
        if (n) cvk_list(list, n, screen.out_full, screen.seq);
    }
}
