// test/vendor: a plugin built from more than its own folder, the way a
// plugin with a vendored emulator is. Not an example to copy: a plugin
// for test/run.sh and sim/test.sh, which build it with
//
//   --src ../more/twice.c         its own code beside src/ (warnings, UBSan)
//   --vendor '../lib/*.c'         someone else's (no warnings, no UBSan)
//   --include ../lib/include      that code's header
//   --include ../more             and its own
//   --define VENDOR_ANSWER=42     a define the vendored code needs
//
// Its note is "vendor 42 14": lib/'s answer, and twice 7 from more/.
#include <stdio.h>

#include "pico/stdlib.h"

#include "answer.h"
#include "cvkernel.h"
#include "cvscreen.h"
#include "handoff.h"
#include "mailbox.h"             // REPORT_KIND_*
#include "rom_image.h"
#include "twice.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static char line[33];

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    cvs_line(c, 16, 16, 224, "VENDOR TEST", C_LYELLOW, C_BLACK, 1);
    cvs_line(c, 16, 40, 224, line, C_WHITE, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    snprintf(line, sizeof line, "vendor %d %d", vendor_answer(3), twice(7));
    cvk_note(line);
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits))
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK) cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);
        const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
        if (n) cvk_list(list, n, screen.out_full, screen.seq);
    }
}
