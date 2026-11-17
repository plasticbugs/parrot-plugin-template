// SD card example: list a folder, read a file, and keep a save file.
//
// The card is the SDK's cvsd (the card itself, a sector at a time) under
// fsro (FAT16, FAT32 and exFAT: folders, long names, files as runs of
// 512-byte sectors). What a plugin can do:
//
//   list any folder          fs_dir_open_path(), fs_dir_next()
//   find a file by its path  fs_find()
//   read it                  fs_open(), then sector k is at fs_lba(k): cvsd_read()
//   make a file of a size    fs_create()   (it never grows or shrinks after)
//   write a file in place    fs_write_sec()
//
// This one counts how many times it has been run, in /SDCARD.SAV: it makes
// the file the first time, and each run reads it, adds one, writes it and
// reads it back.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvscreen.h"
#include "cvsd.h"
#include "handoff.h"
#include "mailbox.h"            // REPORT_KIND_*
#include "rom_image.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];

// fsro's structures are big (the open file's map is 12K): static, not on the stack
static fs_t fs;
static fs_dir_t dir;
static uint8_t sector[512], check[512];

#define ROWS 8
static char row[ROWS][33];      // the folder's listing, as text
static int rows;
static char first_line[33];     // the first file's first characters
static char save_line[33];      // what happened to the save
static const char *error;       // or what went wrong

// A save file's layout is the plugin's business. Put something at its start
// that says it is yours, and a version, so a later release can tell.
typedef struct {
    char magic[8];              // "PCSAVE1"
    uint32_t runs;
} save_t;

// fsro takes the names of files it makes as UTF-16
static int utf16(const char *s, uint16_t *out) {
    int n = 0;
    while (s[n]) { out[n] = (uint8_t)s[n]; n++; }
    return n;
}

static const char *why(int r) {
    switch (r) {
    case FS_EIO:      return "THE CARD DIDN'T ANSWER";
    case FS_ENOFS:    return "NO FAT OR EXFAT ON THE CARD";
    case FS_EFULL:    return "THE CARD IS FULL";
    case FS_ENOFILE:  return "NO SUCH FILE";
    default:          return "THE CARD'S FILE SYSTEM IS ODD";
    }
}

// All of it at once. It takes a second or so (the card's start is most of
// it), with the screen standing still: fine once, at the start. A plugin
// that reads as it runs does it a few sectors a pass (see ../open-file).
static void card_demo(void) {
    // 1. the card, and its file system
    if (!cvsd_card_in()) { error = "NO CARD IN THE SLOT"; return; }
    if (!cvsd_init()) { error = "THE CARD DIDN'T START"; return; }
    int r = fs_mount(&fs, cvsd_read);
    if (r != FS_OK) { error = why(r); return; }
    fs_set_writer(&fs, cvsd_write);         // without this the card is read-only

    // 2. list a folder: here the root; "/NES" or "/SAVES/MINE" work alike
    fs_file_t e, first = {0};
    r = fs_dir_open_path(&fs, &dir, "/");
    if (r != FS_OK) { error = why(r); return; }
    while (rows < ROWS && (r = fs_dir_next(&fs, &dir, &e)) == 1) {
        if (e.attr & (FS_A_HIDDEN | FS_A_SYSTEM)) continue;    // what a computer hides
        if (e.is_dir) snprintf(row[rows++], 33, "%-22.22s  FOLDER", dir.lname);    // dir.lname: the whole name
        else {
            snprintf(row[rows++], 33, "%-22.22s %7lu", dir.lname, (unsigned long)e.size);
            if (!first.size && e.size && strcmp(e.name, "SDCARD.SAV")) first = e;   // (not our own save)
        }
    }
    if (r < 0) { error = why(r); return; }

    // 3. read a file: the first one listed, its first sector
    if (first.size && fs_open(&fs, &first) == FS_OK && cvsd_read(fs_lba(&fs, 0), sector)) {
        int n = 0;
        while (n < 28 && n < (int)first.size && sector[n] >= 32 && sector[n] < 127) n++;
        snprintf(first_line, sizeof first_line, n ? "%.*s" : "(NOT TEXT)", n, (const char *)sector);
    }

    // 4. the save file: found, or made with its final size
    uint16_t name[16];
    const int n = utf16("SDCARD.SAV", name);
    fs_file_t f;
    bool made;
    r = fs_create(&fs, "/", name, n, 512, &f, &made);
    if (r != FS_OK) { error = why(r); return; }
    if (fs_open(&fs, &f) != FS_OK) { error = "THE SAVE WON'T OPEN"; return; }
    save_t save = {"PCSAVE1", 0};
    if (!made) {                // it was there: read it, and trust it only if it is ours
        if (!cvsd_read(fs_lba(&fs, 0), sector)) { error = "THE SAVE DIDN'T READ"; return; }
        if (!memcmp(sector, "PCSAVE1", 8)) memcpy(&save, sector, sizeof save);
    }                           // (a new file holds whatever the card had there: write all of it)
    save.runs++;

    // 5. write it in place, and read it back
    memset(sector, 0, sizeof sector);
    memcpy(sector, &save, sizeof save);
    if (fs_write_sec(&fs, 0, sector) != FS_OK) { error = "THE SAVE DIDN'T WRITE"; return; }
    if (!cvsd_read(fs_lba(&fs, 0), check) || memcmp(sector, check, 512)) { error = "THE SAVE READ BACK WRONG"; return; }
    snprintf(save_line, sizeof save_line, "RUN %lu, SAVED AND READ BACK", (unsigned long)save.runs);

    static char note[33];
    snprintf(note, sizeof note, "SD card example: run %lu", (unsigned long)save.runs);
    cvk_note(note);
}

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    cvs_line(c, 8, 8, 240, "SD CARD", C_LYELLOW, C_BLACK, 2);
    for (int i = 0; i < ROWS; i++) cvs_line(c, 8, 32 + 10 * i, 240, i < rows ? row[i] : "", C_WHITE, C_BLACK, 1);
    cvs_line(c, 8, 120, 240, first_line[0] ? "THE FIRST FILE STARTS" : "", C_GRAY, C_BLACK, 1);
    cvs_line(c, 8, 130, 240, first_line, C_CYAN, C_BLACK, 1);
    if (error) cvs_line(c, 8, 150, 240, error, C_LRED, C_BLACK, 1);
    else cvs_line(c, 8, 150, 240, save_line[0] ? save_line : "READING THE CARD...", C_LGREEN, C_BLACK, 1);
    cvs_line(c, 8, 176, 240, "RESET: BACK TO THE MENU", C_GRAY, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    cvk_note("SD card example");

    const uint32_t t0 = time_us_32();
    uint32_t t_screen = 0;
    bool done = false;
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits))
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK)
                cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);

        // once the console has its first screen ("READING THE CARD...")
        if (!done && screen.lists && !screen.out && t - t0 > 100000) {
            done = true;
            card_demo();
        }

        if (t - t_screen >= 20000) {
            t_screen = t;
            const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
            if (n) cvk_list(list, n, screen.out_full, screen.seq);
        }
    }
}
