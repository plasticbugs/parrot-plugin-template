// host.c -- a plugin's own main.c, run on the Mac.
//
// The plugin is compiled as it is (its main() renamed plugin_main), with
// this file in place of the cart: the SDK's functions it calls are here,
// answered by models of the console, the card, the pads and the PSRAM.
//
//   the console    the two kernels as their sources describe them: the
//                  screen kernel runs each VDP write list a frame after it
//                  was sent, says HELLO and ACK, and reports the controller
//                  (cvkernel.h); the frame kernel takes one list a TV frame,
//                  and each list's time is added up by gbkernel.asm's costs
//                  (cvframes.h). Their VRAM becomes the PNGs.
//   the card       a disk image (PC_CARD), through the SDK's own file system
//   the launch     PC_OPEN=/PATH/ON/THE/CARD: a launch block, as the menu makes it
//   the inputs     PC_SCRIPT, below
//
// It ends at PC_END milliseconds, as if RESET had been pressed, or when the
// script holds both fire buttons and * (joy=fire+fire2 key=*), or a USB
// pad's Select and Start (pad=minus+plus), for 2 seconds (cvhome.h): the screen
// is written to PC_OUT/screen.png (256x192, the size a registry screenshot
// must be), what happened to PC_OUT/log.txt, and every list the plugin sent
// to PC_OUT/lists.bin, which tools/mame-replay.sh plays to the real kernel
// in MAME.
//
// PC_SCRIPT is "MS:WHAT=VALUE ..." in time order:
//   joy=up+fire        the stick and fire buttons held (up down left right fire fire2); joy= lets go
//   key=5              the keypad key held (0-9 * #); key= lets go
//   joy2=... key2=...  the same for controller 2 (the frame kernel reads it with CONTROLLER2)
//   pad=south+left     a USB pad plugged in, holding these (up down left right south east west
//                      north l r zl zr minus plus home capture ls rs); pad=off unplugs it
//   lstick=X,Y rstick=X,Y   its sticks, -2048..2047
//   padkind=hid        what it is: switch (a Switch Pro Controller, the default) or hid
//   pad2=... lstick2=... rstick2=... pad2kind=...   the same for USB pad 2
//   keydown=04 keyup=04     a USB keyboard's key, by HID usage (hex); E0-E7 are the modifiers
//   mods=02            the keyboard's modifiers held (hex, HID's bits: 02 left shift); mods=0 lets go
//   mouse=DX,DY,BUTTONS[,WHEEL]   a USB mouse
//   midiin=90,3C,64    MIDI that comes in (hex bytes): from a MIDI device on a hub when the
//                      port is a host, from the computer when it is a device
//   computer=0|1|ask   the USB port as a device: a computer is listening (1 from the start);
//                      ask: one is there and asking its user, and never takes the cart
//   snap=NAME          PC_OUT/NAME.png now
//   simkey=... simpad=... and the other sim* words: the simulator's own (sim/input.c)
//
// The plugin's USB features (cvusb.h) are the ones its CMakeLists.txt turns
// on: build.sh reads cvcart_features() (with plugin.cmake, as the simulator
// does) and builds this file with the same CVUSB_* switches. The port is
// chosen as cvusb.c chooses it: with MIDI_OUT, a device first, taken by a
// computer 0.3 s in, and a host at 1.5 s if none took it.
//
// A frame plugin built with parrotcart_frames(TARGET CONTROLLER2) is built
// here with CVF_CTRL2 (plugin.cmake reads it): its kernel reads controller
// 2 too, which puts each list 179 T later in its frame (660 for the start,
// not 480), and lists.bin says so for tools/mame-replay.sh.
//
// A frame plugin whose lists run back to back, a sample clock for its sound
// (parrotcart_frames(TARGET BACK_TO_BACK), or PC_BACK_TO_BACK=1), is built
// with CVF_BACK_TO_BACK: a list may then run past its frame, and the model
// checks instead that the console goes straight from it into the next
// (below, "Back to back").
//
// It exits 1 if the plugin broke a rule the models can see: a bad list, a
// list sent before the last was taken, three colours in an 8-pixel run, a
// frame list longer than its frame (or, back to back, shorter than one).

// pread, pwrite and strdup are POSIX. Under -std=c11, Linux's C library
// declares them only when asked (macOS's always does).
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "pico/stdlib.h"

#include "cvframes.h"
#include "cvhome.h"
#include "cvkernel.h"
#include "cvpad.h"
#include "cvpsram.h"
#include "cvscreen.h"
#include "cvsd.h"
#include "cvusb.h"
#include "cvusb_midi.h"
#include "mailbox.h"
#include "tms9918.h"

int plugin_main(void);

// ---- the simulator's hooks --------------------------------------------------------
// sim/model.c builds this file into the simulator (sim/README.md) with these
// defined; here they do nothing.
#ifndef SIM_HOOKS
#define SIM_FRAME()                 // a TV frame has run, its list too
#define SIM_PSG(bytes, n, t)        // the frame kernel writes n PSG bytes, t Z80 cycles into the frame
#define SIM_MAILBOX()               // the plugin publishes the mailbox: mbox is new, mbox_live old
#define SIM_FAIL()                  // a rule broken (fail() has said which)
#define SIM_END()                   // the end, just before the exit
#define SIM_MIDI(bytes, n, to_dev)  // the plugin sends n MIDI bytes: to a computer, or to the MIDI device
#define SIM_EVENT(what, v) false    // a script word the models don't know: the simulator's, if it takes it
#endif

// ---- the test's clock, log and end -----------------------------------------------
#define FRAME_US 16683
static uint64_t now_us = 1000;
static uint64_t end_us = 3000000;
static const char *out_dir = ".";
static FILE *log_file, *listf;
static int fails;
static uint64_t t_task;        // the last cvk_task()/cvf_task(): see polled()
static bool home_off;           // cvk_home(false) / cvf_home(false)
static const char *end_why;     // the controllers' way back to the menu, if that ended it
static bool task_failed;
static void console_step(void);

static void logline(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(log_file, "%6u ", (unsigned)(now_us / 1000));
    vfprintf(log_file, fmt, ap);
    fputc('\n', log_file);
    va_end(ap);
}

static void fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL at %u ms: ", (unsigned)(now_us / 1000));
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    va_start(ap, fmt);
    fprintf(log_file, "%6u FAIL ", (unsigned)(now_us / 1000));
    vfprintf(log_file, fmt, ap);
    fputc('\n', log_file);
    va_end(ap);
    fails++;
    SIM_FAIL();
}

uint32_t time_us_32(void) { now_us += 100; return (uint32_t)now_us; }
void sleep_ms(uint32_t ms) { now_us += (uint64_t)ms * 1000; }
void sleep_us(uint64_t us) { now_us += us; }

// ---- the inputs ------------------------------------------------------------------
static uint8_t joy_held;        // wired.h's WIRED_* order: up right down left fire fire2
static int key_held = -1;
static uint8_t joy2_held;       // controller 2's, the same way
static int key2_held = -1;
static bool pad_in[CVUSB_PAD_MAX];      // USB pads 1 and 2
static gamepad_t pad_state[CVUSB_PAD_MAX];
static const char *pad_kind[CVUSB_PAD_MAX] = {"SWITCH PRO", "SWITCH PRO"};
static bool computer = true, computer_asks;
static uint8_t kb_down[6], kb_mods;
static void usb_key(uint8_t usage, bool down);
static void usb_key_report(void);
static void usb_mouse(int dx, int dy, uint8_t buttons, int wheel);
static void usb_midi_in(const char *hex);
static struct { uint8_t usage; bool down; } kq[64];
static int kq_n, kq_at, kb_count;
static int mouse_dx, mouse_dy, mouse_seen;
static uint8_t mouse_btn;
static char *script;
static const char *script_at;
static void snapshot(const char *name);

static uint32_t names(const char *v, const char *const *tab, int n) {
    uint32_t bits = 0;
    char buf[128];
    strncpy(buf, v, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (char *w = strtok(buf, "+"); w; w = strtok(NULL, "+")) {
        int i = 0;
        while (i < n && strcmp(w, tab[i])) i++;
        if (i == n) { fprintf(stderr, "PC_SCRIPT: what is \"%s\"?\n", w); exit(2); }
        bits |= 1u << i;
    }
    return bits;
}

// "pad", "pad2" ...: which USB pad an event is about (0 or 1), or -1 if it isn't BASE's
static int pad_of(const char *what, const char *base, const char *tail) {
    const size_t n = strlen(base);
    if (strncmp(what, base, n)) return -1;
    const int k = what[n] == '2' ? 1 : 0;
    return !strcmp(what + n + k, tail) ? k : -1;
}

static void event(const char *what, const char *v) {
    static const char *const joy[] = {"up", "right", "down", "left", "fire", "fire2"};
    static const char *const pad[] = {"up", "down", "left", "right", "south", "east", "west", "north", "l", "r",
                                      "zl", "zr", "minus", "plus", "home", "capture", "ls", "rs"};
    int a = 0, b = 0, c = 0, k;
    logline("script %s=%s", what, v);
    if (!strcmp(what, "joy")) joy_held = (uint8_t)names(v, joy, 6);
    else if (!strcmp(what, "key")) key_held = !*v ? -1 : *v == '*' ? 10 : *v == '#' ? 11 : *v - '0';
    else if (!strcmp(what, "joy2")) joy2_held = (uint8_t)names(v, joy, 6);
    else if (!strcmp(what, "key2")) key2_held = !*v ? -1 : *v == '*' ? 10 : *v == '#' ? 11 : *v - '0';
    else if ((k = pad_of(what, "pad", "")) >= 0) {
        pad_in[k] = strcmp(v, "off") != 0;
        if (pad_in[k]) pad_state[k].buttons = names(v, pad, 18);
        else memset(&pad_state[k], 0, sizeof pad_state[k]);
    } else if ((k = pad_of(what, "lstick", "")) >= 0 && sscanf(v, "%d,%d", &a, &b) == 2) { pad_state[k].lx = (int16_t)a; pad_state[k].ly = (int16_t)b; }
    else if ((k = pad_of(what, "rstick", "")) >= 0 && sscanf(v, "%d,%d", &a, &b) == 2) { pad_state[k].rx = (int16_t)a; pad_state[k].ry = (int16_t)b; }
    else if ((k = pad_of(what, "pad", "kind")) >= 0 && (!strcmp(v, "switch") || !strcmp(v, "hid")))
        pad_kind[k] = v[0] == 's' ? "SWITCH PRO" : "HID PAD";
    else if (!strcmp(what, "keydown") || !strcmp(what, "keyup")) usb_key((uint8_t)strtol(v, NULL, 16), what[3] == 'd');
    else if (!strcmp(what, "mods")) {
        kb_count = 1;
        kb_mods = (uint8_t)strtol(v, NULL, 16);
        usb_key_report();
    } else if (!strcmp(what, "mouse") && sscanf(v, "%d,%d,%d", &a, &b, &c) == 3) {
        int w = 0;
        if (sscanf(v, "%*d,%*d,%*d,%d", &w) != 1) w = 0;
        usb_mouse(a, b, (uint8_t)c, w);
    } else if (!strcmp(what, "midiin")) usb_midi_in(v);
    else if (!strcmp(what, "computer")) { computer_asks = !strcmp(v, "ask"); computer = !computer_asks && atoi(v) != 0; }
    else if (!strcmp(what, "snap")) snapshot(v);
    else if (SIM_EVENT(what, v)) {}
    else { fprintf(stderr, "PC_SCRIPT: what is \"%s=%s\"?\n", what, v); exit(2); }
}

static void script_step(void) {
    while (script_at && *script_at) {
        char what[32], v[128] = "";
        unsigned ms;
        int used = 0;
        if (sscanf(script_at, " %u:%31[a-z0-9]=%n", &ms, what, &used) < 2 || !used) { fprintf(stderr, "PC_SCRIPT: can't read \"%s\"\n", script_at); exit(2); }
        if ((uint64_t)ms * 1000 > now_us) return;
        script_at += used;
        int n = 0;
        while (script_at[n] && script_at[n] != ' ' && n < 127) n++;
        memcpy(v, script_at, (size_t)n);
        v[n] = 0;
        script_at += n;
        while (*script_at == ' ') script_at++;
        event(what, v);
    }
}

// ---- PNGs ------------------------------------------------------------------------
static void put32(FILE *f, uint32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(b, 1, 4, f); }

static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
    put32(f, n);
    uint32_t crc = (uint32_t)crc32(0, (const uint8_t *)type, 4);
    fwrite(type, 1, 4, f);
    if (n) { fwrite(d, 1, n, f); crc = (uint32_t)crc32(crc, d, n); }
    put32(f, crc);
}

// 256x192, 16 colours: the console's picture, a pixel a pixel
static void png(const char *name, uint8_t pic[VDP_H][VDP_W]) {
    static uint8_t raw[VDP_H * (1 + VDP_W)], z[sizeof raw + 1024];
    for (int y = 0; y < VDP_H; y++) {
        raw[y * (1 + VDP_W)] = 0;
        memcpy(raw + y * (1 + VDP_W) + 1, pic[y], VDP_W);
    }
    uLongf zn = sizeof z;
    compress2(z, &zn, raw, sizeof raw, 9);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    const uint8_t ihdr[13] = {0, 0, VDP_W >> 8, VDP_W & 255, 0, 0, 0, VDP_H, 8, 3};   // 8 bits, a palette
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "PLTE", &vdp_rgb[0][0], 48);
    chunk(f, "IDAT", z, (uint32_t)zn);
    chunk(f, "IEND", 0, 0);
    fclose(f);
}

// ---- the screen kernel (cvkernel.h; midihello.asm) -------------------------------
static bool k_on, f_on;                 // which kernel the plugin started
static uint8_t k_vram[VRAM_SIZE];       // the console's VRAM
static uint8_t k_reg[8] = {0x02, 0xE0, 0x0E, 0xFF, 0x03, 0x76, 0x03, 0x01};   // its registers, as midihello.asm sets them
static bool k_hello = true;             // it has run no list yet
static uint8_t k_done;                  // the last list it ran, or took as done
static struct { bool out, full; uint8_t seq; size_t n; uint8_t bytes[LIST_SIZE]; } k_list;
static struct { int kind; uint8_t bits; } k_rep[16];
static int k_rep_n;
static uint64_t next_frame = FRAME_US;
static char note[33];
static uint8_t mbox[MB_SIZE], mbox_live[MB_SIZE];
static cvs_t *cvs_seen;
static uint32_t lists_sent, sounds_sent;

// lists.bin, for tools/mame-replay.sh: each list after 9 bytes: its kernel
// ('K' or 'F'), full (for 'F': 1 when the kernel reads controller 2), seq,
// its length, the ms it was sent at (3 bytes), and flags (1: a frame list
// meant to run back to back, which the next must follow without a wait)
static void list_record(char kernel, bool full, uint8_t seq, const uint8_t *b, size_t n, uint8_t flags) {
    const uint8_t h[9] = {(uint8_t)kernel, full, seq, (uint8_t)n, (uint8_t)(n >> 8),
                          (uint8_t)(now_us / 1000), (uint8_t)(now_us / 1000 >> 8), (uint8_t)(now_us / 1000 >> 16), flags};
    fwrite(h, 1, 9, listf);
    fwrite(b, 1, n, listf);
    lists_sent++;
}

static void k_frame(void) {
    if (k_list.out) {                   // LIST_SEQ changed: run it, or (just started, and it isn't a whole screen) skip it
        if (!k_hello || k_list.full) {
            if (!vdp_run(k_vram, k_reg, k_list.bytes, k_list.n)) fail("the console can't run list %u: it is malformed", k_list.seq);
            k_hello = false;
        }
        k_done = k_list.seq;
        k_list.out = false;
    }
    static const uint8_t lines[12] = {10, 13, 7, 12, 2, 3, 14, 5, 1, 11, 9, 6};   // wired.h's table, the other way
    k_rep_n = 0;
    k_rep[k_rep_n].kind = REPORT_KIND_JOY;
    k_rep[k_rep_n++].bits = (uint8_t)(~joy_held & 0x1F);
    k_rep[k_rep_n].kind = REPORT_KIND_KEY;
    k_rep[k_rep_n++].bits = (uint8_t)((key_held < 0 ? 15 : lines[key_held]) | (joy_held & 32 ? 0 : 0x10));
    k_rep[k_rep_n].kind = k_hello ? REPORT_KIND_HELLO : REPORT_KIND_ACK;
    k_rep[k_rep_n++].bits = k_done & 31;
}

void cvk_start(const cvk_config_t *c) {
    (void)c;
    k_on = true;
    t_task = now_us;
    vdp_t v;
    vdp_init(&v);
    memcpy(k_vram, v.vram, VRAM_SIZE);
    memcpy(mbox + MB_SND, cvk_silence, MB_SND_LEN);
    memcpy(mbox_live, mbox, MB_SIZE);
}

const uint8_t cvk_silence[10] = {0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF};
// On the cart cvk_task()/cvf_task() turn the data bus on once the console is
// seen, and take RESET back to the menu. Here the console runs from any SDK
// call, so a loop that polls the console without them (a title screen
// waiting for fire) works on the Mac and shows black or garbage on a
// console. One blocking step, like starting the card, doesn't poll and is
// fine; polling for more than 100 ms without the task call is the bug.
static void polled(const char *task) {
    if (task_failed || now_us - t_task <= 100000) return;
    task_failed = true;
    fail("the plugin polled the console for %u ms without calling %s: on a real console the screen stays "
         "black or garbage and RESET can't get back to the menu. Call it on every pass of every loop, title "
         "screens and waits too", (unsigned)((now_us - t_task) / 1000), task);
}

void cvk_task(uint32_t t) { (void)t; t_task = now_us; console_step(); }
void cvk_home(bool on) { home_off = !on; logline("cvk_home(%s)", on ? "true" : "false"); }
void cvk_note(const char *s) { memset(note, 0, sizeof note); if (s) strncpy(note, s, 32); logline("note \"%s\"", note); }
bool cvk_console_on(void) { return true; }

bool cvk_report(uint32_t t, int *kind, uint8_t *bits) {
    (void)t;
    polled("cvk_task()");
    console_step();
    if (!k_rep_n) return false;
    *kind = k_rep[0].kind;
    *bits = k_rep[0].bits;
    memmove(k_rep, k_rep + 1, sizeof k_rep[0] * (size_t)--k_rep_n);
    return true;
}

void cvk_mb(unsigned i, uint8_t v) { if (i < MB_SIZE) mbox[i] = v; }

void cvk_publish(void) {
    if (mbox[MB_NSEQ] != mbox_live[MB_NSEQ]) logline("noise control %02X (%s noise, rate %u)", mbox[MB_NCTL], mbox[MB_NCTL] & 4 ? "white" : "periodic", mbox[MB_NCTL] & 3);
    if (mbox[MB_SND_SEQ] != mbox_live[MB_SND_SEQ]) {
        const uint8_t *s = mbox + MB_SND;
        char line[128];
        int o = 0;
        for (int ch = 0; ch < 3; ch++) {
            const unsigned n = (s[3 * ch] & 15u) | (s[3 * ch + 1] & 63u) << 4, att = s[3 * ch + 2] & 15u;
            if (att == 15) o += snprintf(line + o, sizeof line - (size_t)o, " -");
            else o += snprintf(line + o, sizeof line - (size_t)o, " %.0fHz@%u", n ? 3579545.0 / (32.0 * n) : 0.0, att);
        }
        logline("sound%s noise@%u", line, s[9] & 15u);
        sounds_sent++;
    }
    SIM_MAILBOX();
    memcpy(mbox_live, mbox, MB_SIZE);
}

void cvk_sound(const uint8_t snd[10]) {
    memcpy(mbox + MB_SND, snd, MB_SND_LEN);
    mbox[MB_SND_SEQ]++;
    cvk_publish();
}

void cvk_list(const uint8_t *list, size_t n, bool full, uint8_t seq) {
    if (k_list.out) fail("list %u sent before the console had run list %u (send one only when cvs_update() returns it)", seq, k_list.seq);
    if (n > LIST_SIZE) { fail("a list of %zu bytes: the console has room for %d", n, LIST_SIZE); n = LIST_SIZE; }
    memcpy(k_list.bytes, list, n);
    k_list.n = n;
    k_list.full = full;
    k_list.seq = seq;
    k_list.out = true;
    list_record('K', full, seq, list, n, 0);
}

// the SDK's own cvs_update (test/run.sh renames it), watched
size_t sdk_cvs_update(cvs_t *s, cvs_draw_fn draw, void *ctx, uint8_t *list, size_t max, uint32_t now);
size_t cvs_update(cvs_t *s, cvs_draw_fn draw, void *ctx, uint8_t *list, size_t max, uint32_t now) {
    cvs_seen = s;
    const uint32_t bad = s->full ? 0 : s->vdp.bad_segments;
    const size_t n = sdk_cvs_update(s, draw, ctx, list, max, now);
    if (s->vdp.bad_segments > bad)
        fail("%u 8-pixel runs were drawn with three colours or more: the console shows two", s->vdp.bad_segments - bad);
    return n;
}

// ---- the frame kernel (cvframes.h; gbkernel.asm) ---------------------------------
#define VBLANK_T (70 * 228)
#define FRAME_T  (262 * 228)
#ifdef CVF_CTRL2
#define START_T  660                    // the list starts ~660 T after the NMI: 179 more, for controller 2
#define NOWAIT_T 533                    // MAME: a list's end to the next one's first command, not waiting (354 + 179)
#else
#define START_T  480                    // the list starts ~480 T after the NMI
#define NOWAIT_T 354                    // MAME: a list's end to the next one's first command, not waiting
#endif
#define NMI_T    105                    // MAME: the kernel's NMI, the BIOS's jump to it included
#define LIST_LIMIT (FRAME_T - 1200)     // a list that isn't back to back ends this early
static uint8_t f_vram[0x4000], f_reg[8] = {0x02, 0xE0, 0x06, 0xFF, 0x03, 0x36, 0x07, 0x01};
static struct { bool waiting; size_t n; uint8_t bytes[CVF_LIST_MAX]; } f_list;
static uint32_t f_frames, f_taken, f_worst_vb, f_worst_all, f_psg_bytes;
static void (*f_exit)(void);

// Back to back (porting.md, "The sound"): a plugin that plays samples makes
// every list a little longer than a frame, and the console goes from one
// list straight into the next, so a list's length is the sample clock. It
// says so with parrotcart_frames(TARGET BACK_TO_BACK) (plugin.cmake reads it:
// CVF_BACK_TO_BACK) or PC_BACK_TO_BACK=1. Then a list past LIST_LIMIT by
// gbkernel.asm's costs is a back-to-back list, and instead of failing it:
//   - its whole length by MAME's costs (a PSG byte 51 T, not 52; NOWAIT_T
//     and NMI_T as MAME runs them) must be over a frame, or the console
//     waits for the next frame after it: a gap in the sound. The header's
//     costs run a little over MAME's, so a list sized by them can fall short
//   - a list that starts right after one that ran into the next frame
//     isn't in the vertical blank, so its blank part must have no video
//     RAM writes (23 T a byte, too fast outside the blank)
// The model keeps the console's own clock: a list starts at the NMI, or at
// the end of one that ran past it; cvf_frames() counts the starts, as the
// cart does; and when no new list is ready the last one runs again.
static bool f_b2b;                      // the plugin's lists may run back to back
static uint64_t f_ticks;                // TV frames (NMIs) so far, for the console's clock
static int64_t f_next = -1;             // T of the next list's first command, or -1: after the next NMI
static uint32_t f_b2b_lists, f_b2b_min = UINT32_MAX, f_b2b_max, f_reruns;
static bool f_ran;                      // a list has run (one to run again)
static struct { size_t n; uint8_t bytes[CVF_LIST_MAX]; } f_last;

// a list's costs: kit, by gbkernel.asm's header from START_T (its end);
// vb, the same at its blank part's end; real, its commands by MAME's costs;
// vb_vram, video RAM bytes in its blank part. False if it's malformed
// (fail() says why, if loud).
typedef struct { uint32_t kit, vb, real, vb_vram; } f_cost_t;
static bool f_cost(const uint8_t *s, size_t n, f_cost_t *c, bool loud) {
    size_t i = 0;
    bool vblank = true;
    *c = (f_cost_t){START_T, 0, 0, 0};
    for (;;) {
        if (i >= n) { if (loud) fail("a frame list without its two ends"); return false; }
        const uint8_t op = s[i];
        if (op == 1) {
            if (i + 4 > n || i + 4 + s[i + 3] > n || !s[i + 3]) { if (loud) fail("a VRAM run past the list's end, or of 0 bytes"); return false; }
            const uint32_t k = 106 + (vblank ? 23u : 31u) * s[i + 3];
            c->kit += k;
            c->real += k;
            if (vblank) c->vb_vram += s[i + 3];
            i += 4u + s[i + 3];
        } else if (op == 2) {
            if (i + 2 + s[i + 1] > n) { if (loud) fail("PSG bytes past the list's end"); return false; }
            c->kit += 75 + 52u * s[i + 1];
            c->real += 75 + 51u * s[i + 1];
            i += 2u + s[i + 1];
        } else if (op == 3 && vblank) {
            c->kit += 114;
            c->real += 114;
            i += 3;
        } else if (op == 0) {
            if (vblank) { vblank = false; c->kit += 54; c->real += 54; c->vb = c->kit; i++; }
            else { c->kit += 52; c->real += 52; return true; }
        } else { if (loud) fail("a frame list's command %u at byte %zu", op, i); return false; }
    }
}

// a back-to-back list: by its costs, in back-to-back mode
static bool f_is_b2b(const f_cost_t *c) { return f_b2b && c->kit > LIST_LIMIT; }

// run a list: its writes into the model's VRAM and registers, its sound to
// the simulator (t0: T from this TV frame's NMI to its first command, and
// real: by MAME's costs, back to back), and the checks. in_blank: it
// started at an NMI, not straight after the list before
static void f_run(const uint8_t *s, size_t n, uint32_t t0, bool real, bool in_blank) {
    f_cost_t c;
    if (!f_cost(s, n, &c, true)) return;
    size_t i = 0;
    uint32_t t = t0;
    bool vblank = true;
    for (;;) {
        const uint8_t op = s[i];
        if (op == 1) {
            uint16_t a = (uint16_t)((s[i + 1] | s[i + 2] << 8) & 0x3FFF);
            if (!(s[i + 2] & 0x40)) fail("a VRAM run's address without $40 in its high byte: the VDP takes it as a read");
            for (int k = 0; k < s[i + 3]; k++) f_vram[(a + k) & 0x3FFF] = s[i + 4 + k];
            t += 106 + (vblank ? 23u : 31u) * s[i + 3];
            i += 4u + s[i + 3];
        } else if (op == 2) {
            char hex[3 * 16 + 1] = "";
            for (int k = 0; k < s[i + 1] && k < 16; k++) snprintf(hex + 3 * k, 4, " %02X", s[i + 2 + k]);
            logline("psg%s", hex);
            SIM_PSG(s + i + 2, s[i + 1], t);
            f_psg_bytes += s[i + 1];
            t += 75 + (real ? 51u : 52u) * s[i + 1];
            i += 2u + s[i + 1];
        } else if (op == 3) {
            f_reg[s[i + 2] & 7] = s[i + 1];
            t += 114;
            i += 3;
        } else if (vblank) { vblank = false; t += 54; i++; }
        else break;
    }
    (void)t;                            // (the simulator's: SIM_PSG)
    if (c.vb > f_worst_vb) f_worst_vb = c.vb;
    if (c.kit > f_worst_all) f_worst_all = c.kit;
    static bool told_vb, told_short;    // (each said once, not every frame)
    if (!in_blank && c.vb_vram) {
        if (!told_vb)
            fail("a list writes %u bytes of video RAM in its blank part, but starts straight after a list that ran into the next frame, "
                 "so it isn't in the blank: too fast there. Put them in its second part", c.vb_vram);
        else fails++;
        told_vb = true;
    } else if (c.vb > VBLANK_T - 600) fail("a list's vertical blank part takes %u T: the blank is %d", c.vb, VBLANK_T);
    if (f_is_b2b(&c)) {
        const uint32_t period = NOWAIT_T + c.real + NMI_T;
        f_b2b_lists++;
        if (period < f_b2b_min) f_b2b_min = period;
        if (period > f_b2b_max) f_b2b_max = period;
        if (period < FRAME_T) {
            if (!told_short)
                fail("a back-to-back list runs %u T by MAME's costs, %u T less than a frame: the console waits for the next frame "
                     "after it, a gap in the sound. Make it longer (gbkernel.asm's costs run a little over MAME's)", period, FRAME_T - period);
            else fails++;
            told_short = true;
        }
    } else if (c.kit > LIST_LIMIT) {
        fail("a list takes %u T: a frame is %d", c.kit, FRAME_T);
    }
}

// the console takes a list: the one waiting, or the last again (back to back)
static void f_take(uint32_t t0, bool in_blank, uint32_t *real) {
    f_cost_t c = {0};
    if (f_list.waiting) {
        f_run(f_list.bytes, f_list.n, t0, f_b2b, in_blank);
        f_cost(f_list.bytes, f_list.n, &c, false);
        memcpy(f_last.bytes, f_list.bytes, f_list.n);
        f_last.n = f_list.n;
        f_ran = true;
        f_list.waiting = false;
        f_taken++;
    } else if (f_b2b && f_ran) {
        f_reruns++;
        f_run(f_last.bytes, f_last.n, t0, true, in_blank);
        f_cost(f_last.bytes, f_last.n, &c, false);
    } else {
        c.real = 54 + 52;               // an empty list (0, 0)
    }
    *real = c.real;
}

static void f_frame(void) {
    if (!f_b2b) {
        f_frames++;
        if (f_list.waiting) {
            f_run(f_list.bytes, f_list.n, START_T, false, true);
            f_list.waiting = false;
            f_taken++;
        }
        return;
    }
    // the console's clock, in T: this NMI, and the list starts before the next
    const uint64_t nmi = f_ticks++ * (uint64_t)FRAME_T;
    const bool waited = f_next < 0;
    if (waited) f_next = (int64_t)(nmi + START_T);
    bool in_blank = waited;
    while ((uint64_t)f_next < nmi + FRAME_T) {
        f_frames++;                     // the kernel reads $FFF0+seq
        uint32_t real;
        f_take((uint32_t)((uint64_t)f_next - nmi), in_blank, &real);
        uint64_t end = (uint64_t)f_next + real, next_nmi = nmi + FRAME_T;
        bool past = false;
        while (next_nmi <= end) { end += NMI_T; next_nmi += FRAME_T; past = true; }
        if (!past) { f_next = -1; break; }  // it waits for the next NMI
        f_next = (int64_t)(end + NOWAIT_T);
        in_blank = false;
    }
}

void cvd_start(void) {}
void cvd_stage(uint32_t n) { (void)n; }
void cvf_start(const cvf_config_t *c) {
    (void)c;
    f_on = true;
    t_task = now_us;
    f_vram[0x1B00] = 0xD0;
#ifdef CVF_BACK_TO_BACK
    f_b2b = true;
#endif
    if (getenv("PC_BACK_TO_BACK") && strcmp(getenv("PC_BACK_TO_BACK"), "0")) f_b2b = true;
}
void cvf_task(uint32_t t) { (void)t; t_task = now_us; console_step(); }
void cvf_home(bool on) { cvk_home(on); }
void cvf_note(const char *s) { cvk_note(s); }
void cvf_note_now(void) {}
void cvf_on_exit(void (*fn)(void)) { f_exit = fn; }
uint32_t cvf_frames(void) { polled("cvf_task()"); console_step(); return f_frames; }
bool cvf_waiting(void) { return f_list.waiting; }
bool cvf_console_on(void) { return true; }
// what the two-sample bus loop (cvfserve.S) refused on the console's bus:
// the model's bus is clean, so nothing
void cvf_bus_counts(uint32_t *unsel, uint32_t *early, uint32_t *unhdr) { *unsel = *early = *unhdr = 0; }

// a controller as the frame kernel reports it (gbkernel.asm), low = pressed
static void f_controller(uint8_t held, int key_down, uint8_t *joy, uint8_t *fire, uint8_t *key) {
    static const uint8_t lines[12] = {10, 13, 7, 12, 2, 3, 14, 5, 1, 11, 9, 6};
    *joy = (uint8_t)(~held & 15);
    *fire = (uint8_t)((held & 16 ? 0 : 1) | (held & 32 ? 0 : 2));
    *key = key_down < 0 ? 15 : lines[key_down];
}

void cvf_input(uint8_t *joy, uint8_t *fire, uint8_t *key) { f_controller(joy_held, key_held, joy, fire, key); }
#ifdef CVF_CTRL2
void cvf_input2(uint8_t *joy, uint8_t *fire, uint8_t *key) { f_controller(joy2_held, key2_held, joy, fire, key); }
#endif

bool cvf_publish(const uint8_t *list, size_t n) {
    if (f_list.waiting) return false;
    if (n > CVF_LIST_MAX) { fail("a frame list of %zu bytes: CVF_LIST_MAX is %d", n, CVF_LIST_MAX); return false; }
    memcpy(f_list.bytes, list, n);
    f_list.n = n;
    f_list.waiting = true;
    f_cost_t c;
    const uint8_t b2b = f_cost(list, n, &c, false) && f_is_b2b(&c);
#ifdef CVF_CTRL2
    list_record('F', 1, 0, list, n, b2b);   // (1: the kernel reads controller 2, for tools/mame-replay.sh)
#else
    list_record('F', false, 0, list, n, b2b);
#endif
    return true;
}

// ---- the launch block, the card --------------------------------------------------
static cvl_t block;
static bool launched;
static int card_fd = -1;
static cvsd_info_t sd_info = {.hc = true, .mb = 64, .khz = 25000, .name = "MAC"};

const cvl_t *cvk_launch(void) { return launched ? &block : NULL; }
const cvl_t *cvf_launch(void) { return launched ? &block : NULL; }
bool cvsd_card_in(void) { return card_fd >= 0; }
bool cvsd_init(void) { now_us += 300000; return card_fd >= 0; }       // a card's start takes a while
// the card as the menu left it: started, so no wait (false with none, as the
// cart's says when there's no card that answers)
bool cvsd_resume(void) {
    sd_info.resumed = card_fd >= 0;
    return card_fd >= 0;
}
const cvsd_info_t *cvsd_info(void) { return &sd_info; }

bool cvsd_read(uint32_t lba, uint8_t *buf) {
    now_us += 600;
    sd_info.rd_sectors++;
    return card_fd >= 0 && pread(card_fd, buf, 512, (off_t)lba * 512) == 512;
}

bool cvsd_write(uint32_t lba, const uint8_t *buf) {
    now_us += 3000;
    sd_info.wr_sectors++;
    return card_fd >= 0 && pwrite(card_fd, buf, 512, (off_t)lba * 512) == 512;
}

int cvsd_open_launched(fs_t *fs, const cvl_t *b, bool writable, fs_file_t *f) {   // as sdk/cvsd.c
    cvsd_launched_file(b, f);
    if (!b->path[0]) return FS_ENOFILE;
    if (!cvsd_card_in() || !cvsd_init()) return FS_EIO;
    int r = fs_mount(fs, cvsd_read);
    if (r != FS_OK) return r;
    if (writable) fs_set_writer(fs, cvsd_write);
    return fs_open(fs, f);
}

// the menu's part: find the file, fill in the block
static void launch(const char *id, const char *path) {
    memset(&block, 0, sizeof block);
    block.magic = CVL_LAUNCH;
    block.api = CVL_API;
    block.seq = 1;
    strncpy(block.plugin, id, sizeof block.plugin - 1);
    if (path && *path) {
        static fs_t fs;
        fs_file_t e;
        if (card_fd < 0 || fs_mount(&fs, cvsd_read) || fs_find(&fs, path, &e) || e.is_dir) { fprintf(stderr, "PC_OPEN: %s isn't a file on the card image\n", path); exit(2); }
        strncpy(block.path, path, sizeof block.path - 1);
        block.file_size = e.size;
        block.file_clus = e.first_clus;
        block.file_contig = e.contig;
    }
    cvl_seal(&block);
    launched = true;
}

// ---- USB pads (cvpad.h), keyboard and mouse, MIDI out (the examples' own) ----------
void cvpad_init(void) { logline("usb: the port is a host"); }
void cvpad_task(void) {}
bool cvpad_get(int n, gamepad_t *p) {
    if (n < 0 || n >= CVUSB_PAD_MAX || !pad_in[n]) return false;
    *p = pad_state[n];
    return true;
}
int cvpad_count(void) { return pad_in[0] + pad_in[1]; }
bool cvpad_seen(void) { return pad_in[0] || pad_in[1]; }
uint32_t cvpad_task_max_us(void) { return 50; }
const char *cvpad_kind(int n) { return n >= 0 && n < CVUSB_PAD_MAX && pad_in[n] ? pad_kind[n] : NULL; }
bool cvpad_info(int n, cvpad_info_t *o) { (void)n; (void)o; return false; }
void cvpad_info_lap(int n) { (void)n; }
void cvpad_set_log(void (*fn)(const char *)) { (void)fn; }
void cvpad_set_tick(void (*fn)(void)) { (void)fn; }

uint32_t cvpad_dirs(const gamepad_t *p) {                 // as sdk/cvpad.c
    uint32_t d = p->buttons & (GP_UP | GP_DOWN | GP_LEFT | GP_RIGHT);
    if (!d) {
        if (p->ly > 820) d |= GP_UP;
        if (p->ly < -820) d |= GP_DOWN;
        if (p->lx < -820) d |= GP_LEFT;
        if (p->lx > 820) d |= GP_RIGHT;
    }
    return d;
}

void usbin_init(void) { logline("usb: the port is a host"); }
void usbin_task(void) {}
void usbin_count(int *kb, int *mice) { *kb = kb_count; *mice = mouse_seen; }
bool usbin_mouse(int *dx, int *dy, uint8_t *b) { *dx = mouse_dx; *dy = mouse_dy; *b = mouse_btn; mouse_dx = mouse_dy = 0; return mouse_seen; }
bool usbin_key(uint8_t *usage, uint8_t *mods, bool *down) {
    if (kq_at >= kq_n) return false;
    *usage = kq[kq_at].usage;
    *mods = 0;
    *down = kq[kq_at++].down;
    return true;
}

static bool midi_on;
static uint32_t midi_bytes;
void usbmidi_start(void) { midi_on = true; logline("usb: the port is a MIDI device"); }
void usbmidi_stop(void) { midi_on = false; }
void usbmidi_task(void) {}
bool usbmidi_mounted(void) { return midi_on && computer; }
bool usbmidi_host_seen(void) { return midi_on && computer; }
uint32_t usbmidi_dropped(void) { return 0; }
bool usbmidi_write(const uint8_t *m, uint32_t n) {
    char hex[3 * 16 + 1] = "";
    for (uint32_t k = 0; k < n && k < 16; k++) snprintf(hex + 3 * k, 4, " %02X", m[k]);
    logline("midi%s", hex);
    midi_bytes += n;
    SIM_MIDI(m, n, false);
    return true;
}

// ---- cvusb.h: the SDK's USB features ------------------------------------------------
static cvusb_mode_t usb_mode;
static uint64_t usb_t0;
static bool usb_midi_dev;               // a MIDI device on the hub (it has sent something)
static cvkeys_t usb_keys;
static cvkey_t usb_kq[32];              // as sdk/cvusb.c's: rings that keep 31 and 63
static int usb_kq_head, usb_kq_tail;
static cvmouse_t usb_mice;
static struct { int n; uint8_t m[3]; } usb_mq[64];
static int usb_mq_head, usb_mq_tail;
static uint32_t usb_midi_dropped;

static void usb_mode_now(cvusb_mode_t m) {
    if (m == usb_mode) return;
    usb_mode = m;
    if (m == CVUSB_HOST) logline("usb: the port is a host");
    if (m == CVUSB_DEVICE) logline("usb: the port is a MIDI device, on a computer");
    if (m == CVUSB_ASKING) logline("usb: a computer is asking its user");
}

// the keyboard's reports, read by the SDK's own cvusb_keys.c
static void usb_key_event(const cvkey_t *k, void *ctx) {
    (void)ctx;
    const int next = (usb_kq_tail + 1) % 32;
    if (usb_mode != CVUSB_HOST || !CVUSB_KEYBOARD || next == usb_kq_head) return;     // full: dropped
    usb_kq[usb_kq_tail] = *k;
    usb_kq_tail = next;
}

static void usb_key_report(void) {
    uint8_t r[8] = {kb_mods, 0};
    memcpy(r + 2, kb_down, 6);
    cvkeys_report(&usb_keys, r, 8, usb_key_event, NULL);
}

// a USB keyboard's key went down or up, by its HID usage: the modifiers
// (E0-E7) are bits of its boot report's first byte, the rest among its six
// keys; for the SDK's reader (cvusb_keys.c), and the example's own (usbin_*)
static void usb_key(uint8_t u, bool down) {
    kb_count = 1;
    if (kq_at >= kq_n) kq_at = kq_n = 0;    // all taken: the queue starts again
    if (kq_n < 64) { kq[kq_n].usage = u; kq[kq_n++].down = down; }
    if (u >= 0xE0 && u <= 0xE7) {
        const uint8_t bit = (uint8_t)(1u << (u - 0xE0));
        kb_mods = down ? (uint8_t)(kb_mods | bit) : (uint8_t)(kb_mods & ~bit);
    } else {
        for (int i = 0; i < 6; i++) if (kb_down[i] == u) kb_down[i] = 0;
        if (down) for (int i = 0; i < 6; i++) if (!kb_down[i]) { kb_down[i] = u; break; }
    }
    usb_key_report();
}

// a USB mouse moved, or its buttons or wheel did: for the SDK's reader, and
// the example's own
static void usb_mouse(int dx, int dy, uint8_t buttons, int wheel) {
    mouse_seen = 1;
    mouse_dx += dx;
    mouse_dy += dy;
    mouse_btn = buttons;
    if (usb_mode != CVUSB_HOST || !CVUSB_MOUSE) return;
    do {                                // a report moves 127 at most
        const int x = dx > 127 ? 127 : dx < -127 ? -127 : dx, y = dy > 127 ? 127 : dy < -127 ? -127 : dy;
        const int w = wheel > 127 ? 127 : wheel < -127 ? -127 : wheel;
        const uint8_t r[4] = {buttons, (uint8_t)x, (uint8_t)y, (uint8_t)w};
        cvmouse_report(&usb_mice, r, 4);
        dx -= x;
        dy -= y;
        wheel -= w;
    } while (dx || dy || wheel);
}

// MIDI bytes as USB-MIDI packets and back, by the SDK's own cvusb_midi.c:
// what a MIDI device on the hub plays (a host), or what the computer sends
// (a device)
static void usb_midi_bytes(const uint8_t *bytes, uint32_t n) {
    uint8_t pk[64][4], st = 0, m[3];
    if (usb_mode == CVUSB_HOST) usb_midi_dev = true;
    const bool on = (usb_mode == CVUSB_HOST && CVUSB_MIDI_IN) || (usb_mode == CVUSB_DEVICE && CVUSB_MIDI_OUT);
    const int k = cvmidi_pack(bytes, n, pk, 64, &st);
    for (int i = 0; on && i < k; i++) {
        const int len = cvmidi_msg(pk[i], m);
        if (!len) continue;
        const int next = (usb_mq_tail + 1) % 64;
        if (next == usb_mq_head) { usb_midi_dropped++; continue; }
        usb_mq[usb_mq_tail].n = len;
        memcpy(usb_mq[usb_mq_tail].m, m, 3);
        usb_mq_tail = next;
    }
}

static void usb_midi_in(const char *hex) {
    uint8_t bytes[64];
    uint32_t n = 0;
    for (char *p = (char *)hex; *p && n < sizeof bytes;) {
        bytes[n++] = (uint8_t)strtol(p, &p, 16);
        if (*p == ',') p++;
    }
    usb_midi_bytes(bytes, n);
}

void cvusb_start(void) {
    if (usb_mode != CVUSB_NONE) return;
    usb_t0 = now_us;
    if (CVUSB_MIDI_OUT) {
        usb_mode = CVUSB_WAITING;
        logline("usb: looking for a computer");
    } else {
        usb_mode_now(CVUSB_HOST);
    }
}

void cvusb_host_now(void) {
    if (CVUSB_HOST_FEATURES && (usb_mode == CVUSB_WAITING || usb_mode == CVUSB_ASKING)) usb_mode_now(CVUSB_HOST);
}

void cvusb_task(void) {
    console_step();
    if (usb_mode != CVUSB_WAITING && usb_mode != CVUSB_ASKING) return;
    const uint64_t dt = now_us - usb_t0;
    if (computer && dt >= 300000) usb_mode_now(CVUSB_DEVICE);
    else if (computer_asks && dt >= 700000 && dt < 60000000) usb_mode_now(CVUSB_ASKING);
    else if (CVUSB_HOST_FEATURES && dt >= (computer_asks ? 60000000u : 1500000u)) usb_mode_now(CVUSB_HOST);
}

cvusb_mode_t cvusb_mode(void) { return usb_mode; }

const char *cvusb_mode_name(cvusb_mode_t m) {            // as sdk/cvusb.c
    switch (m) {
    case CVUSB_WAITING: return "LOOKING FOR A COMPUTER";
    case CVUSB_ASKING:  return "ALLOW IT ON THE COMPUTER";
    case CVUSB_DEVICE:  return "ON A COMPUTER";
    case CVUSB_HOST:    return "HOST";
    default:            return "OFF";
    }
}

void cvusb_status(cvusb_status_t *s) {
    memset(s, 0, sizeof *s);
    if (usb_mode == CVUSB_HOST) {
        s->pads = (uint8_t)(CVUSB_PADS ? cvpad_count() : 0);
        s->keyboards = (uint8_t)(CVUSB_KEYBOARD && kb_count);
        s->mice = (uint8_t)(CVUSB_MOUSE && mouse_seen);
        s->midi = CVUSB_MIDI_IN && usb_midi_dev;
    }
    if (usb_mode == CVUSB_DEVICE) s->midi = true;
}

uint32_t cvusb_task_max_us(void) { return 50; }
bool cvusb_pad(int n, gamepad_t *p) { return usb_mode == CVUSB_HOST && CVUSB_PADS && cvpad_get(n, p); }
bool cvpad_held(uint32_t buttons) {
    for (int n = 0; n < CVUSB_PAD_MAX; n++)
        if (pad_in[n] && (pad_state[n].buttons & buttons) == buttons) return true;
    return false;
}
const char *cvusb_pad_kind(int n) { return usb_mode == CVUSB_HOST && CVUSB_PADS ? cvpad_kind(n) : NULL; }
uint32_t cvusb_pad_dirs(const gamepad_t *p) { return cvpad_dirs(p); }

bool cvusb_key(cvkey_t *k) {
    if (usb_kq_head == usb_kq_tail) return false;
    *k = usb_kq[usb_kq_head];
    usb_kq_head = (usb_kq_head + 1) % 32;
    return true;
}

bool cvusb_mouse(cvmouse_t *m) {
    *m = usb_mice;
    usb_mice.dx = usb_mice.dy = usb_mice.wheel = 0;
    return usb_mode == CVUSB_HOST && CVUSB_MOUSE && mouse_seen;
}

int cvusb_midi_read(uint8_t msg[3]) {
    if (usb_mq_head == usb_mq_tail) return 0;
    const int n = usb_mq[usb_mq_head].n;
    memcpy(msg, usb_mq[usb_mq_head].m, 3);
    usb_mq_head = (usb_mq_head + 1) % 64;
    return n;
}

bool cvusb_midi_send(const uint8_t *m, uint32_t n) {
    const bool to_computer = usb_mode == CVUSB_DEVICE && CVUSB_MIDI_OUT;
    const bool to_device = usb_mode == CVUSB_HOST && CVUSB_MIDI_IN && usb_midi_dev;
    if (!to_computer && !to_device) return false;
    char hex[3 * 16 + 1] = "";
    for (uint32_t k = 0; k < n && k < 16; k++) snprintf(hex + 3 * k, 4, " %02X", m[k]);
    logline("midi%s%s", hex, to_device ? " (to the MIDI device)" : "");
    midi_bytes += n;
    SIM_MIDI(m, n, to_device);
    return true;
}

uint32_t cvusb_midi_dropped(void) { return usb_midi_dropped; }

// ---- the PSRAM -------------------------------------------------------------------
uint8_t host_psram[CVPSRAM_SIZE];
bool cvpsram_init(void) { return !getenv("PC_NO_PSRAM"); }
const uint8_t *cvpsram_id(void) { static const uint8_t id[8] = {0x0D, 0x5D, 0x52, 0xA2, 0x64, 0x31, 0x91, 0x31}; return id; }

// ---- the frames, and the end -------------------------------------------------------
// the TV's picture: either kernel's VRAM and registers, as the VDP shows them (tms9918.h)
static void picture(uint8_t pic[VDP_H][VDP_W]) {
    tms_picture(f_on ? f_vram : k_vram, f_on ? f_reg : k_reg, pic);
}

static void snapshot(const char *name) {
    static uint8_t pic[VDP_H][VDP_W];
    picture(pic);
    png(name, pic);
}

static void finish(void) {
    static bool done;
    if (done) return;
    done = true;
    if (end_why) logline("%s held for 2 s", end_why);
    if (k_on) k_frame();                // a list still out runs
    if (f_on) f_frame();
    if (f_b2b_lists)
        logline("%u lists back to back, %u to %u T each by MAME's costs, %u run again", f_b2b_lists, f_b2b_min, f_b2b_max, f_reruns);
    logline("%s: back to the menu, its bottom line \"%s\"", end_why ? "the controllers" : "RESET", note);
    if (f_exit) f_exit();
    if (k_on && cvs_seen && memcmp(k_vram, cvs_seen->vdp.vram, VRAM_SIZE)) fail("the console's VRAM isn't what the SDK thinks it is");
    snapshot("screen");
    printf("  %u ms, %u lists", (unsigned)(now_us / 1000), lists_sent);
    if (k_on) printf(", %u sound changes", sounds_sent);
    if (f_on) printf(" (%u taken in %u frames; longest: blank part %u T of %d, list %u T of %d), %u PSG bytes", f_taken, f_frames,
                     f_worst_vb, VBLANK_T, f_worst_all, FRAME_T, f_psg_bytes);
    if (f_b2b_lists) printf(", %u lists back to back (%u to %u T each by MAME's costs, %u run again)", f_b2b_lists, f_b2b_min, f_b2b_max, f_reruns);
    if (midi_bytes) printf(", %u MIDI bytes", midi_bytes);
    printf(", note \"%s\"%s\n", note, fails ? " -- FAILED" : "");
    fclose(log_file);
    fclose(listf);
    SIM_END();
    exit(fails ? 1 : 0);
}

// back to the menu from the controllers, as cvk_task() and cvf_task() look for it
static void home_step(void) {
    static cvhome_t home;
    if (home_off || !(k_on || f_on)) return;
    const bool console = cvhome_console(joy_held & 16, joy_held & 32, key_held == 10 ? 9 : 15);
    bool pad = false;
    for (int n = 0; n < CVUSB_PAD_MAX; n++)
        if (pad_in[n] && (pad_state[n].buttons & CVHOME_PAD) == CVHOME_PAD) pad = true;
    if (!cvhome_step(&home, (uint32_t)now_us, console || pad)) return;
    end_why = console ? "both fire buttons and *" : "a USB pad's Select and Start";
    finish();
}

static void console_step(void) {
    while (now_us >= next_frame) {
        next_frame += FRAME_US;
        script_step();
        if (k_on) k_frame();
        if (f_on) f_frame();
        SIM_FRAME();
        home_step();
    }
    if (now_us >= end_us) finish();
}

int main(void) {
    char path[1024];
    if (getenv("PC_OUT")) out_dir = getenv("PC_OUT");
    if (getenv("PC_END")) end_us = (uint32_t)atoi(getenv("PC_END")) * 1000u;
    snprintf(path, sizeof path, "%s/log.txt", out_dir);
    log_file = fopen(path, "w");
    snprintf(path, sizeof path, "%s/lists.bin", out_dir);
    listf = fopen(path, "wb");
    if (!log_file || !listf) { perror(out_dir); return 2; }
    if (getenv("PC_SCRIPT")) script_at = script = strdup(getenv("PC_SCRIPT"));
    if (getenv("PC_CARD") && (card_fd = open(getenv("PC_CARD"), O_RDWR)) < 0) { perror(getenv("PC_CARD")); return 2; }
    if (!getenv("PC_NO_LAUNCH")) launch(getenv("PC_ID") ? getenv("PC_ID") : "test.plugin", getenv("PC_OPEN"));
    plugin_main();
    fail("the plugin's main() returned");
    finish();
    return 1;
}
