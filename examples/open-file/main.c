// Opening a file from the menu: a text viewer.
//
// This plugin's CMakeLists.txt says OPENS .txt. From then on, picking a
// .txt file in the cart's menu and pressing A starts this plugin, and the
// menu tells it which file through the launch block (cvlaunch.h): the
// file's path as the menu shows it, and where on the card it is.
//
//   cvk_launch()            the block, or NULL (started some other way)
//   block->path             "/NOTES/READ ME.TXT", or "" when the plugin was
//                           started from the Plugins page, with no file
//   cvsd_open_launched()    the card mounted and that file opened
//
// Open the file through cvsd_open_launched(), not by looking its path up
// again: the path is for showing (a character outside ASCII is '?' in it).
//
// The stick scrolls. Going back, the menu returns to the same folder with
// the same file selected, and shows the note the plugin left.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvscreen.h"
#include "cvsd.h"
#include "handoff.h"
#include "rom_image.h"
#include "wired.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static wired_t pad;
static fs_t fs;
static fs_file_t file;

#define COLS    30
#define ROWS    17
#define MAX_TEXT 8192           // as much of the file as is kept: 16 sectors
static uint8_t text[MAX_TEXT + 512];
static uint32_t text_len, sectors_read, sectors;
#define MAX_LINES 2048
static uint16_t line_at[MAX_LINES];         // where each line of the screen starts in text
static int lines, top;
static const char *name = "";
static const char *message = "OPEN A .TXT FILE FROM THE MENU";

// the text as lines of the screen: cut at a line's end, or at 30 characters
static void wrap(void) {
    lines = 0;
    for (uint32_t i = 0; i < text_len && lines < MAX_LINES;) {
        line_at[lines++] = (uint16_t)i;
        uint32_t n = 0;
        while (i + n < text_len && n < COLS && text[i + n] != '\n') n++;
        i += n;
        if (i < text_len && text[i] == '\n') i++;
    }
}

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    cvs_line(c, 8, 4, 240, name, C_LYELLOW, C_BLACK, 1);
    for (int r = 0; r < ROWS; r++) {
        char s[COLS + 1] = "";
        if (top + r < lines) {
            const uint8_t *p = text + line_at[top + r];
            int n = 0;
            while (n < COLS && p + n < text + text_len && p[n] != '\n' && p[n] != '\r') { s[n] = (char)(p[n] == '\t' ? ' ' : p[n]); n++; }
            s[n] = 0;
        }
        cvs_line(c, 8, 18 + 9 * r, 240, s, C_WHITE, C_BLACK, 1);
    }
    cvs_line(c, 8, 180, 240, message, C_GRAY, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    wired_init(&pad);

    // The launch block: read it before anything else might go wrong. It
    // stays where it is until the plugin goes back.
    const cvl_t *block = cvk_launch();
    const bool have_file = block && block->path[0];
    if (have_file) {
        const char *slash = strrchr(block->path, '/');
        name = slash ? slash + 1 : block->path;
        message = "READING...";
    }

    const uint32_t t0 = time_us_32();
    uint32_t t_screen = 0;
    bool opened = false;
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

        if (have_file && !opened && t - t0 > 20000) {
            // The card's start blocks for up to a second and a half: once,
            // after the console has been served long enough to boot.
            opened = true;
            const int r = cvsd_open_launched(&fs, block, false, &file);    // false: we won't write to it
            if (r != FS_OK) {
                message = r == FS_EIO ? "THE CARD DIDN'T ANSWER" : "THE FILE WON'T OPEN";
                cvk_note("Text Viewer couldn't read it");
            } else {
                sectors = (uint32_t)((file.size + 511) / 512);
                if (sectors > MAX_TEXT / 512) sectors = MAX_TEXT / 512;
            }
        } else if (sectors_read < sectors) {
            // The file, a sector a pass, so the loop keeps turning. Sector
            // k of the open file is at fs_lba(&fs, k) on the card.
            if (!cvsd_read(fs_lba(&fs, sectors_read), text + 512 * sectors_read)) {
                message = "A READ FAILED";
                sectors = sectors_read;
            } else if (++sectors_read == sectors) {
                text_len = file.size < MAX_TEXT ? (uint32_t)file.size : MAX_TEXT;
                wrap();
                message = file.size > MAX_TEXT ? "STICK: SCROLL (THE FIRST 8K)" : "STICK: SCROLL   RESET: MENU";
                static char note[33];
                snprintf(note, sizeof note, "Read %.26s", name);
                cvk_note(note);
            }
        }

        // the stick: a line a press, a page with the fire button held
        const uint8_t hit = wired_pressed(&pad);
        const int step = pad.held & WIRED_FIRE_L ? ROWS : 1;
        if (hit & WIRED_DOWN) top += step;
        if (hit & WIRED_UP) top -= step;
        if (top > lines - ROWS) top = lines - ROWS;
        if (top < 0) top = 0;

        if (t - t_screen >= 20000) {
            t_screen = t;
            const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
            if (n) cvk_list(list, n, screen.out_full, screen.seq);
        }
    }
}
