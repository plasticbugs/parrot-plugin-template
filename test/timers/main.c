// test/timers: the pico-sdk's alarms and repeating timers, which the
// models run on their own clock (test/host/include/pico/time.h). Not an
// example to copy: a plugin for test/run.sh and sim/test.sh.
//
// Its note says, as the run goes on:
//
//   note "timers 499 99 1 0 50"
//
// the ticks of a timer every 2 ms (start to start), of one 10 ms after each
// tick ends, whether the alarm at 500 ms has gone off, the ticks of a timer
// cancelled at once (none), and the 2 ms timer's ticks during a 100 ms
// sleep_ms() at the start.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvscreen.h"
#include "handoff.h"
#include "mailbox.h"             // REPORT_KIND_*
#include "rom_image.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static volatile uint32_t fast, slow, fired, cancelled;
static char line[33];

static bool on_fast(repeating_timer_t *t) {
    (void)t;
    fast++;
    return true;
}

static bool on_slow(repeating_timer_t *t) {
    (void)t;
    slow++;
    return true;
}

static bool on_cancelled(repeating_timer_t *t) {
    (void)t;
    cancelled++;
    return true;
}

static int64_t on_alarm(alarm_id_t id, void *user) {
    (void)id;
    (void)user;
    fired++;
    return 0;                           // once
}

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    cvs_line(c, 16, 16, 224, "TIMER TEST", C_LYELLOW, C_BLACK, 1);
    cvs_line(c, 16, 40, 224, line, C_WHITE, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    static repeating_timer_t t_fast, t_slow, t_cancelled;
    add_repeating_timer_us(-2000, on_fast, NULL, &t_fast);
    add_repeating_timer_ms(10, on_slow, NULL, &t_slow);
    add_repeating_timer_ms(5, on_cancelled, NULL, &t_cancelled);
    cancel_repeating_timer(&t_cancelled);
    add_alarm_in_ms(500, on_alarm, NULL, false);
    sleep_ms(100);                      // (one long step at the start, then the task call)
    const uint32_t in_sleep = fast;
    uint32_t t_step = 0;
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits))
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK) cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);
        if (t - t_step < 20000) continue;
        t_step = t;
        snprintf(line, sizeof line, "timers %lu %lu %lu %lu %lu", (unsigned long)fast, (unsigned long)slow,
                 (unsigned long)fired, (unsigned long)cancelled, (unsigned long)in_sleep);
        cvk_note(line);
        const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
        if (n) cvk_list(list, n, screen.out_full, screen.seq);
    }
}
