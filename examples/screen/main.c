// Screen example: text and tile graphics.
//
// The plugin draws into a canvas, 256x192 pixels in the console's 15
// colours, and the SDK sends the console only the bytes that changed. The
// drawing calls are vdp.h's and cvscreen.h's:
//
//   canvas_fill(c, x, y, w, h, colour)             a rectangle
//   canvas_text(c, x, y, "TEXT", colour, scale)    8x8 font, upper case, any pixel position
//   cvs_line(c, x, y, w, "TEXT", fg, bg, scale)    text in a box of its own (so it replaces what was there)
//
// The one rule is the console's video chip's: every 8-pixel run of a row
// (x 0-7, 8-15, ...) shows two colours at most. Keep coloured things on
// the 8-pixel grid, or over black, and it takes care of itself.
#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvscreen.h"
#include "handoff.h"
#include "rom_image.h"
#include "wired.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static wired_t pad;

// ---- tiles: 8x8, two colours, drawn where you like -------------------------------
typedef const char *tile_t[8];
static const tile_t BRICK = {"#######.", "#######.", "#######.", "........", "###.####", "###.####", "###.####", "........"};
static const tile_t COIN  = {"..####..", ".######.", "###..###", "###.####", "###.####", "###..###", ".######.", "..####.."};
static const tile_t BALL  = {"..####..", ".######.", "########", "########", "########", "########", ".######.", "..####.."};

static void stamp(canvas_t *c, int x, int y, const tile_t t, uint8_t fg, uint8_t bg) {
    for (int r = 0; r < 8; r++)
        for (int b = 0; b < 8; b++) canvas_fill(c, x + b, y + r, 1, 1, t[r][b] == '#' ? fg : bg);
}

// a map of tiles, a character a tile, as a game would keep its level
static const char *const MAP[3] = {
    "##############################",
    "#  o   o   o   ##   o   o   o#",
    "##############################",
};

// ---- the ball: something that moves, to show that only changes are sent ----------
#define BOX_X 8
#define BOX_Y 112
#define BOX_W 240
#define BOX_H 56
static int ball_x = 40, ball_y = 20, ball_dx = 2, ball_dy = 1;
static uint8_t ball_colour = C_LRED;

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)ctx;
    if (all) {                  // the console's screen is blank: the parts that never change
        canvas_text(c, 8, 8, "TEXT AND TILES", C_LYELLOW, 2);
        canvas_text(c, 8, 32, "8X8 FONT, 32 COLUMNS, 24 ROWS", C_WHITE, 1);
        for (int i = 0; i < 15; i++) canvas_fill(c, 8 + 16 * i, 48, 16, 12, (uint8_t)(i + 1));
        canvas_text(c, 8, 64, "15 COLOURS, 2 IN ANY 8 PIXELS", C_GRAY, 1);
        for (int row = 0; row < 3; row++)
            for (int col = 0; MAP[row][col]; col++) {
                const char t = MAP[row][col];
                if (t == '#') stamp(c, 8 + 8 * col, 80 + 8 * row, BRICK, C_DRED, C_GRAY);
                if (t == 'o') stamp(c, 8 + 8 * col, 80 + 8 * row, COIN, C_LYELLOW, C_BLACK);
            }
        canvas_text(c, 8, 176, "FIRE: COLOUR   RESET: THE MENU", C_GRAY, 1);
    }
    // the part that changes: all of it again, every time. Drawing a pixel
    // the colour it already is costs nothing on the console.
    canvas_fill(c, BOX_X, BOX_Y, BOX_W, BOX_H, C_BLACK);
    stamp(c, BOX_X + ball_x, BOX_Y + ball_y, BALL, ball_colour, C_BLACK);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    wired_init(&pad);
    cvk_note("Screen example");

    uint32_t t_screen = 0;
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits)) {
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK)
                cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);
            else
                wired_report(&pad, kind, bits);
        }
        if (wired_pressed(&pad) & (WIRED_FIRE_L | WIRED_FIRE_R))
            ball_colour = ball_colour == C_WHITE ? C_MGREEN : (uint8_t)(ball_colour + 1);

        if (t - t_screen >= 20000) {        // 50 times a second: move, then draw
            t_screen = t;
            ball_x += ball_dx;
            ball_y += ball_dy;
            if (ball_x <= 0 || ball_x >= BOX_W - 8) ball_dx = -ball_dx;
            if (ball_y <= 0 || ball_y >= BOX_H - 8) ball_dy = -ball_dy;
            // cvs_update() calls draw() only when the console is ready for
            // a new list; a busy screen just updates a little less often.
            const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
            if (n) cvk_list(list, n, screen.out_full, screen.seq);
        }
    }
}
