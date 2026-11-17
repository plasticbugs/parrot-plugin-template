// PSRAM example: the cart's 8 MB of extra memory.
//
// A plugin's code and data share the chip's 512K of fast RAM. Anything big
// -- a game's ROM, a picture, a level -- goes in the PSRAM: 8 MB, mapped
// into memory like any other, a little slower (it is read through a 16K
// cache). After cvpsram_init():
//
//   CVPSRAM_CACHED     a uint8_t pointer to all 8 MB: read and write it
//                      like an array
//   CVPSRAM_UNCACHED   the same memory past the cache, for filling it in
//                      32-bit words (cvpsram.h says when that matters)
//
// It holds nothing useful at the start, and keeps nothing once the console
// is off. This example fills all of it with a pattern, reads it back, and
// times both.
#include <stdio.h>

#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvpsram.h"
#include "cvscreen.h"
#include "handoff.h"
#include "mailbox.h"            // REPORT_KIND_*
#include "rom_image.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];

#define STEP 0x10000u           // 64K a pass of the loop: a few milliseconds
enum { START, WRITE, READ, DONE, NO_PSRAM };
static int phase = START;
static uint32_t at, errors, write_us, read_us;

// a byte for every address that isn't the same 64K, 1 MB or 4 MB further on:
// a memory that answers at the wrong address is caught
static inline uint8_t pattern(uint32_t a) { return (uint8_t)(a ^ a >> 8 ^ a >> 16 ^ a >> 21); }

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    char s[33];
    cvs_line(c, 16, 8, 224, "PSRAM", C_LYELLOW, C_BLACK, 2);
    if (phase == NO_PSRAM) {
        cvs_line(c, 16, 40, 224, "THE PSRAM DIDN'T ANSWER", C_LRED, C_BLACK, 1);
    } else if (phase != START) {
        const uint8_t *id = cvpsram_id();
        snprintf(s, sizeof s, "ID %02X %02X %02X  8 MB", id[0], id[1], id[2]);
        cvs_line(c, 16, 40, 224, s, C_WHITE, C_BLACK, 1);
        cvs_line(c, 16, 64, 224, phase == WRITE ? "WRITING" : phase == READ ? "READING BACK" : "DONE", C_WHITE, C_BLACK, 1);
        const int done = (int)((phase == DONE ? CVPSRAM_SIZE : at) / (CVPSRAM_SIZE / 28));     // a bar of 28 characters
        canvas_fill(c, 16, 80, 8 * done, 8, C_LGREEN);
        canvas_fill(c, 16 + 8 * done, 80, 8 * (28 - done), 8, C_DBLUE);
        if (write_us) {
            snprintf(s, sizeof s, "WRITE %lu MS", (unsigned long)(write_us / 1000));
            cvs_line(c, 16, 104, 224, s, C_WHITE, C_BLACK, 1);
        }
        if (read_us) {
            snprintf(s, sizeof s, "READ  %lu MS", (unsigned long)(read_us / 1000));
            cvs_line(c, 16, 116, 224, s, C_WHITE, C_BLACK, 1);
        }
        if (phase == DONE) {
            snprintf(s, sizeof s, errors ? "%lu BYTES WRONG" : "ALL 8 MB READ BACK RIGHT", (unsigned long)errors);
            cvs_line(c, 16, 140, 224, s, errors ? C_LRED : C_LGREEN, C_BLACK, 1);
        }
    }
    cvs_line(c, 16, 176, 224, "RESET: BACK TO THE MENU", C_GRAY, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    cvk_note("PSRAM example");

    uint32_t t_screen = 0;
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits))
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK)
                cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);

        // the work, a 64K step a pass
        uint8_t *const ram = CVPSRAM_CACHED;
        const uint32_t t_step = time_us_32();
        switch (phase) {
        case START:
            phase = cvpsram_init() ? WRITE : NO_PSRAM;      // once, after cvk_start()
            if (phase == NO_PSRAM) cvk_note("PSRAM example: no PSRAM");
            break;
        case WRITE:
            for (uint32_t a = at; a < at + STEP; a++) ram[a] = pattern(a);
            write_us += time_us_32() - t_step;
            if ((at += STEP) == CVPSRAM_SIZE) { at = 0; phase = READ; }
            break;
        case READ:
            for (uint32_t a = at; a < at + STEP; a++) errors += ram[a] != pattern(a);
            read_us += time_us_32() - t_step;
            if ((at += STEP) == CVPSRAM_SIZE) {
                phase = DONE;
                cvk_note(errors ? "PSRAM example: errors" : "PSRAM example: 8 MB right");
            }
            break;
        }

        if (t - t_screen >= 20000) {
            t_screen = t;
            const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
            if (n) cvk_list(list, n, screen.out_full, screen.seq);
        }
    }
}
