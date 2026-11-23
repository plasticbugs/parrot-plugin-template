// Two controllers example: both of the console's controllers and USB pad 1,
// live, on the frame kernel.
//
// The frame kernel reads controller 1 every frame. With CONTROLLER2 in
// parrotcart_frames() (CMakeLists.txt) it reads controller 2 as well, and
// cvf_input2() has it, encoded as cvf_input() encodes controller 1: low =
// pressed, the stick in bits 0-3 (up right down left), the fire buttons in
// bits 0 and 1, and the keypad as its 4 lines ($F: none). Reading it costs
// the kernel 179 Z80 cycles at the start of every frame, which is why it
// is a switch.
//
// The screen shows each controller's stick (a 3x3 grid, its way lit), its
// fire buttons, its keypad and the three nibbles as they came; then USB pad
// 1's two sticks, and what a twin-stick game makes of it all, by the kit's
// rule: controller 1's stick moves and controller 2's fires, and on a USB
// pad the left stick moves and the right stick fires (or the face buttons,
// for a pad whose right stick doesn't read).
//
// The picture is drawn into a 256x192 pixel buffer, turned into the
// console's pattern and colour tables, and each frame's list carries the
// bytes that changed, as examples/frames does.
#include <string.h>

#include "pico/stdlib.h"

#include "cvdiag.h"
#include "cvframes.h"
#include "cvinput.h"            // cvin_decode_key()
#include "cvusb.h"
#include "font8.h"
#include "gbkernel.h"           // made by the build: the frame kernel that reads controller 2 too
#include "handoff.h"

// ---- the twin-stick rule ---------------------------------------------------------
// Ways as gamepad.h's GP_UP GP_DOWN GP_LEFT GP_RIGHT, opposite ways at once cancelled
static uint32_t ways(uint32_t d) {
    if ((d & (GP_UP | GP_DOWN)) == (GP_UP | GP_DOWN)) d &= ~(uint32_t)(GP_UP | GP_DOWN);
    if ((d & (GP_LEFT | GP_RIGHT)) == (GP_LEFT | GP_RIGHT)) d &= ~(uint32_t)(GP_LEFT | GP_RIGHT);
    return d;
}

// a console stick's 4 bits (low = pressed: up 1, right 2, down 4, left 8)
static uint32_t stick_ways(uint8_t joy) {
    return (joy & 1 ? 0 : GP_UP) | (joy & 2 ? 0 : GP_RIGHT) | (joy & 4 ? 0 : GP_DOWN) | (joy & 8 ? 0 : GP_LEFT);
}

// a USB pad's analog stick past about 40% each way, as cvusb_pad_dirs() does for the left one
static uint32_t analog_ways(int x, int y) {
    return (y > 820 ? GP_UP : 0) | (y < -820 ? GP_DOWN : 0) | (x < -820 ? GP_LEFT : 0) | (x > 820 ? GP_RIGHT : 0);
}

// ---- the picture -----------------------------------------------------------------
#define W 256
#define H 192
static uint8_t pix[H][W];                   // the console's colours, a pixel each

enum { BLACK = 1, MGREEN = 2, LGREEN = 3, DBLUE = 4, LBLUE = 5, DRED = 6, CYAN = 7, MRED = 8, LRED = 9,
       DYELLOW = 10, LYELLOW = 11, DGREEN = 12, MAGENTA = 13, GRAY = 14, WHITE = 15 };

static void box(int x, int y, int w, int h, uint8_t c) {
    for (int j = y; j < y + h && j < H; j++)
        for (int i = x; i < x + w && i < W; i++) pix[j][i] = c;
}

// text in the 8x8 font (upper case), each character on its own 8-pixel run
static void text(int x, int y, const char *s, uint8_t fg, uint8_t bg) {
    for (; *s && x < W; s++, x += 8) {
        const int ch = *s >= 'a' && *s <= 'z' ? *s - 32 : *s;
        const uint8_t *g = font8[ch >= 32 && ch < 96 ? ch - 32 : 0];
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b++) pix[y + r][x + b] = g[r] & (0x80 >> b) ? fg : bg;
    }
}

static char hexdigit(unsigned v) { return "0123456789ABCDEF"[v & 15]; }

static const char *way_name(uint32_t d) {
    switch (ways(d)) {
    case GP_UP: return "UP";
    case GP_DOWN: return "DOWN";
    case GP_LEFT: return "LEFT";
    case GP_RIGHT: return "RIGHT";
    case GP_UP | GP_LEFT: return "UP-LEFT";
    case GP_UP | GP_RIGHT: return "UP-RIGHT";
    case GP_DOWN | GP_LEFT: return "DOWN-LEFT";
    case GP_DOWN | GP_RIGHT: return "DOWN-RIGHT";
    default: return "-";
    }
}

// one controller's panel at x0: stick, fires, keypad, the nibbles as they came
static void panel(int x0, const char *title, uint8_t joy, uint8_t fire, uint8_t key) {
    text(x0, 24, title, CYAN, BLACK);
    // the stick: a 3x3 grid of 16x16 cells, the cell for its way lit (the middle when let go)
    const uint32_t d = ways(stick_ways(joy));
    const int cx = d & GP_LEFT ? 0 : d & GP_RIGHT ? 2 : 1, cy = d & GP_UP ? 0 : d & GP_DOWN ? 2 : 1;
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
            box(x0 + 16 * i, 40 + 16 * j, 14, 14, i == cx && j == cy ? (d ? LYELLOW : GRAY) : DBLUE);
    // the fire buttons
    text(x0 + 56, 40, "FIRE L", fire & 1 ? GRAY : BLACK, fire & 1 ? BLACK : LRED);
    text(x0 + 56, 52, "FIRE R", fire & 2 ? GRAY : BLACK, fire & 2 ? BLACK : LRED);
    // the keypad, the key held drawn dark on yellow
    static const char *const rows[4] = {"123", "456", "789", "*0#"};
    static const int8_t keys[4][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {CVIN_STAR, 0, CVIN_HASH}};
    const int held = cvin_decode_key(key);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++) {
            const char s[2] = {rows[r][c], 0};
            const bool on = keys[r][c] == held;
            text(x0 + 56 + 16 * c, 68 + 10 * r, s, on ? BLACK : WHITE, on ? LYELLOW : BLACK);
        }
    // the three nibbles, as cvf_input() or cvf_input2() gave them
    const char raw[] = {'J', hexdigit(joy), ' ', 'F', hexdigit(fire), ' ', 'K', hexdigit(key), 0};
    text(x0, 96, raw, GRAY, BLACK);
}

// a pad's stick: a dot in a 48x48 square
static void analog(int x0, int y0, int x, int y, bool there) {
    box(x0, y0, 48, 48, there ? DBLUE : BLACK);
    if (!there) return;
    const int px = (x + 2048) * 44 / 4096, py = (2047 - y) * 44 / 4096;     // y up -> the screen's down
    box(x0 + px, y0 + py, 4, 4, WHITE);
}

static void render(void) {
    uint8_t j1, f1, k1, j2, f2, k2;
    cvf_input(&j1, &f1, &k1);                       // controller 1
    cvf_input2(&j2, &f2, &k2);                      // controller 2
    gamepad_t p;
    const bool pad = cvusb_pad(0, &p);
    if (!pad) memset(&p, 0, sizeof p);

    // the menu's note: what the two controllers gave last (it shows after RESET)
    static char note[32];
    const char now[] = {'C', '1', ' ', 'J', hexdigit(j1), ' ', 'F', hexdigit(f1), ' ', 'K', hexdigit(k1), ',', ' ',
                        'C', '2', ' ', 'J', hexdigit(j2), ' ', 'F', hexdigit(f2), ' ', 'K', hexdigit(k2), 0};
    if (strcmp(note, now)) {
        strcpy(note, now);
        cvf_note(note);
    }

    memset(pix, BLACK, sizeof pix);
    text(8, 8, "TWO CONTROLLERS", LYELLOW, BLACK);
    panel(8, "CONTROLLER 1", j1, f1, k1);
    panel(136, "CONTROLLER 2", j2, f2, k2);

    const char *kind = cvusb_pad_kind(0);
    text(8, 116, "USB PAD 1", CYAN, BLACK);
    text(88, 116, pad ? kind : kind ? "STARTING" : "NONE", WHITE, BLACK);
    analog(8, 128, p.lx, p.ly, pad);                // its left stick
    analog(64, 128, p.rx, p.ry, pad);               // its right stick
    text(8, 180, "LEFT   RIGHT", GRAY, BLACK);

    // the twin-stick rule: what moves, what fires
    const uint32_t b = p.buttons;
    const uint32_t move = stick_ways(j1) | (pad ? cvusb_pad_dirs(&p) : 0);
    const uint32_t shoot = stick_ways(j2) | analog_ways(p.rx, p.ry) | (b & GP_NORTH ? GP_UP : 0) |
                           (b & GP_SOUTH ? GP_DOWN : 0) | (b & GP_WEST ? GP_LEFT : 0) | (b & GP_EAST ? GP_RIGHT : 0);
    text(128, 136, "TWIN STICK", CYAN, BLACK);
    text(128, 152, "MOVE", GRAY, BLACK);
    text(168, 152, way_name(move), WHITE, BLACK);
    text(128, 164, "FIRE", GRAY, BLACK);
    text(168, 164, way_name(shoot), WHITE, BLACK);
}

// ---- pixels to the console's tables -----------------------------------------------
#define TABLE   6144
#define PATTERN 0x0000
#define COLOUR  0x2000
#define NAMES   0x1800
static uint8_t want[2 * TABLE];             // patterns, then colours
static uint8_t shown[2 * TABLE];            // what the console has, once every list so far has run

// each 8x1 run: its two colours, the higher one the pattern's 1s (one colour: 0s only)
static void encode(void) {
    for (int y = 0; y < H; y++)
        for (int tx = 0; tx < 32; tx++) {
            const uint8_t *r = &pix[y][tx * 8];
            uint8_t lo = r[0], hi = r[0];
            for (int b = 1; b < 8; b++) {
                if (r[b] < lo) lo = r[b];
                if (r[b] > hi) hi = r[b];
            }
            uint8_t bits = 0;
            for (int b = 0; b < 8; b++)
                if (r[b] == hi && hi != lo) bits |= (uint8_t)(0x80 >> b);
            const int i = (y / 8 * 32 + tx) * 8 + (y & 7);
            want[i] = bits;
            want[TABLE + i] = (uint8_t)(hi << 4 | lo);
        }
}

// ---- lists (examples/frames has these explained) ----------------------------------
#define FRAME_T     (262 * 228)
#define BLANK_T     (70 * 228)
#define BLANK_LIMIT (BLANK_T - 2000)
#define LIST_LIMIT  (FRAME_T - 4000)
#define START_T     660                     // the kernel's start of a frame, with controller 2 (gbkernel.asm)
#define RUN_T       106
#define PSG_T(n)    (75 + 52 * (n))
#define END_T       54

static uint8_t list[CVF_LIST_MAX];
static size_t len;
static uint32_t cost, cursor;

static void diff(uint32_t limit, uint32_t per_byte) {
    for (uint32_t scanned = 0; scanned < 2 * TABLE;) {
        const uint32_t i = cursor, table_end = i < TABLE ? TABLE : 2 * TABLE;
        if (want[i] == shown[i]) {
            cursor = (cursor + 1) % (2 * TABLE);
            scanned++;
            continue;
        }
        if (cost + RUN_T + per_byte > limit) return;
        uint32_t n = 0, same = 0;
        while (n < 255 && i + n < table_end && cost + RUN_T + per_byte * (n + 1) <= limit) {
            same = want[i + n] == shown[i + n] ? same + 1 : 0;
            if (same == 4) break;
            n++;
        }
        n -= same == 4 ? 3 : same;
        const uint16_t address = (uint16_t)(i < TABLE ? PATTERN + i : COLOUR + i - TABLE);
        list[len++] = 1;
        list[len++] = (uint8_t)address;
        list[len++] = (uint8_t)(address >> 8 | 0x40);
        list[len++] = (uint8_t)n;
        memcpy(list + len, want + i, n);
        memcpy(shown + i, want + i, n);
        len += n;
        cost += RUN_T + per_byte * n;
        cursor = (i + n) % (2 * TABLE);
        scanned += n;
    }
}

// the whole sound chip, silent, in every list
static void silence(void) {
    static const uint8_t psg[10] = {0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF};
    list[len++] = 2;
    list[len++] = sizeof psg;
    memcpy(list + len, psg, sizeof psg);
    len += sizeof psg;
    cost += PSG_T(sizeof psg);
}

// the names, so that the tables are a bitmap
static void first_list(void) {
    len = 0;
    list[len++] = 0;
    for (int third = 0; third < 3; third++)
        for (int half = 0; half < 2; half++) {
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
    cvd_start();
    cvusb_start();              // USB pads, through a powered hub

    first_list();
    bool pending = true;
    uint32_t seen = 0;
    for (;;) {
        cvf_task(time_us_32());
        cvusb_task();
        if (pending && cvf_publish(list, len)) pending = false;
        if (pending || cvf_frames() == seen) continue;
        seen = cvf_frames();                        // a frame has started: the next list

        render();
        encode();
        len = 0;
        cost = START_T;
        silence();
        diff(BLANK_LIMIT, 23);
        list[len++] = 0;
        cost += END_T;
        diff(LIST_LIMIT, 31);
        list[len++] = 0;
        pending = true;
    }
}
