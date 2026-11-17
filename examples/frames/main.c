// Frame lists example: a whole picture, made on the cart, 60 times a second.
//
// The other examples draw now and then and let the SDK send the changes.
// An emulator or a video player makes a new picture every TV frame and
// needs to say exactly what the console does with each one. For that the
// console runs the SDK's other kernel, which does one thing: at the start
// of every frame it asks the cart for a command list, and runs it.
//
// A list is bytes (cvframes.h and gbkernel.asm have all of it):
//
//   1, low, high | $40, n, n bytes    write n bytes of video memory at an address
//   2, n, n bytes                     write n bytes to the sound chip
//   3, value, $80 | r                 set video register r   (first part only)
//   0                                 the end of a part
//
// in two parts, each ended by a 0: the first runs during the TV's vertical
// blank, when video memory takes a byte every 23 Z80 cycles; the second
// while the picture is being drawn, a byte every 31. A frame is 59,736
// cycles, and a list that runs longer than its frame makes the picture
// stutter, so the plugin adds up what a list costs as it builds it.
//
// That is room for about 1,800 bytes a frame, and a full picture is 12,288
// (a 6K pattern table and a 6K colour table: every 8x1 run of pixels has a
// pattern byte and a colour byte). So a plugin keeps a copy of what the
// console's video memory holds, and each frame sends the bytes that differ
// from the picture it wants, as many as fit -- this file's diff().
//
// The picture here: colour bars and a ball. The console's stick steers it.
#include <string.h>

#include "pico/stdlib.h"

#include "cvdiag.h"
#include "cvframes.h"
#include "gbkernel.h"           // made by the build: the frame kernel
#include "handoff.h"

// ---- the picture -----------------------------------------------------------------
// Video memory as the frame kernel sets it up (Graphics II): patterns at
// $0000, colours at $2000, each 6144 bytes, and 768 names at $1800 that say
// which tile is where. With name i = tile i the screen is a plain bitmap.
#define TABLE   6144
#define PATTERN 0x0000
#define COLOUR  0x2000
#define NAMES   0x1800
static uint8_t want[2 * TABLE];             // the picture we want: patterns, then colours
static uint8_t shown[2 * TABLE];            // what the console has, once every list so far has run
static int ball_x = 100, ball_y = 60, ball_dx = 2, ball_dy = 1;

static const uint8_t BAR[8] = {4, 5, 7, 12, 2, 3, 10, 11};     // the console's colours, 32 pixels each

static void render(void) {
    for (int y = 0; y < 192; y++)
        for (int tx = 0; tx < 32; tx++) {
            uint8_t bits = 0;                       // this 8x1 run: the ball's pixels...
            for (int b = 0; b < 8; b++) {
                const int dx = tx * 8 + b - ball_x, dy = y - ball_y;
                if (dx * dx + dy * dy < 12 * 12) bits |= (uint8_t)(0x80 >> b);
            }
            const int i = (y / 8 * 32 + tx) * 8 + (y & 7);
            want[i] = bits;
            want[TABLE + i] = (uint8_t)(0xF0 | BAR[tx / 4]);    // ...white (15), on the bar's colour
        }
}

// ---- lists -----------------------------------------------------------------------
// What each command costs in Z80 cycles, from gbkernel.asm's header
#define FRAME_T     (262 * 228)
#define BLANK_T     (70 * 228)
#define BLANK_LIMIT (BLANK_T - 2000)        // the first part must end inside the blank...
#define LIST_LIMIT  (FRAME_T - 4000)        // ...and the list well inside the frame
#define RUN_T       106
#define PSG_T(n)    (75 + 52 * (n))
#define END_T       54

static uint8_t list[CVF_LIST_MAX];
static size_t len;
static uint32_t cost;
static uint32_t cursor;                     // where diff() got to: it carries on from there

// bytes of `want` that differ from `shown`, as runs, until `limit` cycles
// are used up at per_byte cycles a byte
static void diff(uint32_t limit, uint32_t per_byte) {
    for (uint32_t scanned = 0; scanned < 2 * TABLE;) {
        const uint32_t i = cursor, table_end = i < TABLE ? TABLE : 2 * TABLE;
        if (want[i] == shown[i]) {
            cursor = (cursor + 1) % (2 * TABLE);
            scanned++;
            continue;
        }
        if (cost + RUN_T + per_byte > limit) return;
        // a run from here: up to 255 bytes, to the table's end, while the
        // time lasts, stopping at 4 bytes in a row that are right already
        uint32_t n = 0, same = 0;
        while (n < 255 && i + n < table_end && cost + RUN_T + per_byte * (n + 1) <= limit) {
            same = want[i + n] == shown[i + n] ? same + 1 : 0;
            if (same == 4) break;
            n++;
        }
        n -= same == 4 ? 3 : same;                  // not the right ones at its end
        const uint16_t address = (uint16_t)(i < TABLE ? PATTERN + i : COLOUR + i - TABLE);
        list[len++] = 1;
        list[len++] = (uint8_t)address;
        list[len++] = (uint8_t)(address >> 8 | 0x40);
        list[len++] = (uint8_t)n;
        memcpy(list + len, want + i, n);
        memcpy(shown + i, want + i, n);             // it will be so, once this list has run
        len += n;
        cost += RUN_T + per_byte * n;
        cursor = (i + n) % (2 * TABLE);
        scanned += n;
    }
}

// the sound chip's bytes for voice 0: a tone, or silence
static void beep(bool on) {
    static const uint8_t ON[3] = {0x8E, 0x0F, 0x92}, OFF[1] = {0x9F};   // 440 Hz, fairly loud; volume off
    const uint8_t n = on ? 3 : 1;
    list[len++] = 2;
    list[len++] = n;
    memcpy(list + len, on ? ON : OFF, n);
    len += n;
    cost += PSG_T(n);
}

// The first list: the names (tile i at place i, in each third of the
// screen), so that the tables are a bitmap. Everything in a list must be
// safe to run twice: when the plugin has nothing new, the console runs the
// last list again.
static void first_list(void) {
    len = 0;
    list[len++] = 0;                                // nothing in the blank part
    for (int third = 0; third < 3; third++)
        for (int half = 0; half < 2; half++) {      // 256 names as two runs of 128
            const uint16_t address = (uint16_t)(NAMES + 256 * third + 128 * half);
            list[len++] = 1;
            list[len++] = (uint8_t)address;
            list[len++] = (uint8_t)(address >> 8 | 0x40);
            list[len++] = 128;
            for (int i = 0; i < 128; i++) list[len++] = (uint8_t)(128 * half + i);
        }
    list[len++] = 0;
}

int main(void) {
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);            // first: the console is parked, waiting
    cvd_start();                // a crash becomes a note on the menu's bottom line (cvdiag.h)
    cvf_note("Frames example");

    first_list();
    bool pending = true;        // a list is built and not yet handed over
    uint32_t seen = 0;
    int beep_frames = 0;
    for (;;) {
        cvf_task(time_us_32());                     // every pass: power, and RESET back to the menu
        // Never drop a list: `shown` already counts on it. Offer it until it is taken.
        if (pending && cvf_publish(list, len)) pending = false;
        if (pending || cvf_frames() == seen) continue;
        seen = cvf_frames();                        // a TV frame has started: make the list for the next

        // the controller, as the console read it at the frame's start (low = pressed)
        uint8_t joy, fire, key;
        cvf_input(&joy, &fire, &key);
        if (!(joy & 1)) ball_dy = -2;               // up
        if (!(joy & 4)) ball_dy = 2;                // down
        if (!(joy & 8)) ball_dx = -2;               // left
        if (!(joy & 2)) ball_dx = 2;                // right
        ball_x += ball_dx;
        ball_y += ball_dy;
        bool bounced = false;
        if (ball_x < 12 || ball_x > 243) { ball_dx = -ball_dx; ball_x += 2 * ball_dx; bounced = true; }
        if (ball_y < 12 || ball_y > 179) { ball_dy = -ball_dy; ball_y += 2 * ball_dy; bounced = true; }
        render();

        len = 0;
        cost = 480;                                 // the kernel's own start of a frame
        if (bounced) { beep(true); beep_frames = 4; }
        else if (beep_frames && !--beep_frames) beep(false);
        diff(BLANK_LIMIT, 23);                      // the blank part: 23 cycles a byte
        list[len++] = 0;
        cost += END_T;
        diff(LIST_LIMIT, 31);                       // while the picture is drawn: 31
        list[len++] = 0;
        pending = true;
    }
}
