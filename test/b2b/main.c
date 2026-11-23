// test/b2b: lists that run back to back, made wrong on purpose, for the
// models' checks (test/run.sh back-to-back). Not an example to copy: a
// plugin for the Mac only (it reads the environment); examples/samples is
// the one that does it right. Run with PC_BACK_TO_BACK=1.
//
//   PC_B2B_T=N   each list's commands take N cycles by gbkernel.asm's costs
//                (59,700 is long enough; 59,200 is short of a frame in MAME)
//   PC_B2B_VB=1  each list also writes 8 bytes of video RAM in its blank part
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvframes.h"
#include "gbkernel.h"
#include "handoff.h"

static uint8_t list[CVF_LIST_MAX];

int main(void) {
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);
    cvf_note("B2B test");
    const uint32_t want = getenv("PC_B2B_T") ? (uint32_t)atoi(getenv("PC_B2B_T")) : 59700;
    const bool vb = getenv("PC_B2B_VB") && atoi(getenv("PC_B2B_VB"));
    // each list: (8 bytes in the blank part,) its end, the sound chip's
    // ten bytes, then video runs of up to 255 bytes at $3800 (no table
    // there) to come to N, and the end
    static const uint8_t quiet[10] = {0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF};
    size_t len = 0;
    uint32_t cost = 0;
    if (vb) {
        list[len++] = 1;
        list[len++] = 0x00;
        list[len++] = 0x40 | 0x38;
        list[len++] = 8;
        memset(list + len, 0x55, 8);
        len += 8;
        cost += 106 + 23 * 8;
    }
    list[len++] = 0;
    cost += 54;
    list[len++] = 2;
    list[len++] = sizeof quiet;
    memcpy(list + len, quiet, sizeof quiet);
    len += sizeof quiet;
    cost += 75 + 52 * sizeof quiet;
    while (cost + 106 + 31 + 52 <= want) {
        uint32_t n = (want - 52 - cost - 106) / 31;
        if (n > 255) n = 255;
        list[len++] = 1;
        list[len++] = 0x00;
        list[len++] = 0x40 | 0x38;
        list[len++] = (uint8_t)n;
        memset(list + len, 0xAA, n);
        len += n;
        cost += 106 + 31 * n;
    }
    list[len++] = 0;
    uint32_t seen = 0;
    bool pending = true;
    for (;;) {
        cvf_task(time_us_32());
        if (pending && cvf_publish(list, len)) pending = false;
        if (pending || cvf_frames() == seen) continue;
        seen = cvf_frames();
        pending = true;
    }
}
