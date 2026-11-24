// test/font: the SDK's 6x8 font (cvfont.c) and its text editor (cvedit.c)
// on the screen kernel. Not an example to copy: a plugin for test/run.sh
// and sim/test.sh, which build the SDK's optional parts into a plugin that
// calls them, as cvfont_use() and cvedit_use() do for the cart.
//
// The TV shows every character of the font, and an editor below it. A USB
// keyboard types into the editor, and the note is what it holds, its line
// ends as |: note "font: Hi|there" once the script has typed that.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvedit.h"
#include "cvfont.h"
#include "cvkernel.h"
#include "cvscreen.h"
#include "cvusb.h"
#include "handoff.h"
#include "mailbox.h"             // REPORT_KIND_*
#include "rom_image.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static cvedit_t ed;             // (big: static)
static uint8_t text[4096];

// the editor's card: none (it edits, and a save would say there's no card)
static fs_t *no_card(void *ctx, bool failed) {
    (void)ctx;
    (void)failed;
    return NULL;
}

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)ctx;
    cvfont_line(c, 8, 0, 40, "cvfont: The quick brown fox, 0123456789.", C_LYELLOW, C_BLACK);
    for (int r = 0; r < 3; r++) {               // space to ~, 32 a line
        char row[33];
        int n = 0;
        for (int ch = 32 + 32 * r; ch < 64 + 32 * r && ch < 127; ch++) row[n++] = (char)ch;
        row[n] = 0;
        cvfont_line(c, 8, 8 + 8 * r, 40, row, C_WHITE, C_DBLUE);
    }
    cvedit_draw(&ed, c, all);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    const cvedit_config_t ec = {.text = text, .cap = sizeof text, .x = 8, .y = 40, .cols = 40, .rows = 18,
                                .ext = ".txt", .home = "/TEXT", .stem = "NOTE",
                                .keys = "TYPE ON A USB KEYBOARD", .card = no_card};
    cvedit_init(&ed, &ec);
    cvusb_start();
    uint32_t t_step = 0;
    char said[33] = "";
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        cvusb_task();
        cvkey_t k;
        while (cvusb_key(&k)) cvedit_key(&ed, &k);
        cvedit_task(&ed, t);
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits))
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK) cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);
        if (t - t_step < 20000) continue;
        t_step = t;
        char note[33];
        int n = snprintf(note, sizeof note, "font: ");
        for (uint32_t i = 0; i < ed.len && n < 32; i++) note[n++] = text[i] == '\n' ? '|' : (char)text[i];
        note[n] = 0;
        if (strcmp(note, said)) {
            strcpy(said, note);
            cvk_note(note);
        }
        const size_t len = cvs_update(&screen, draw, NULL, list, sizeof list, t);
        if (len) cvk_list(list, len, screen.out_full, screen.seq);
    }
}
