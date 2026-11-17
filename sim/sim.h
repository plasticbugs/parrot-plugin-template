// sim.h -- the Parrot Cart plugin simulator (sim/README.md).
//
// A plugin's own sources, built for the desktop against the console's
// models (test/host/host.c, built in by model.c), with a window for the TV,
// SDL audio for the sound chip, and the keyboard and game pads for the
// controllers. The parts:
//
//   main.c    options, the window, each TV frame, RESET, the end
//   model.c   host.c's models, and the few calls the rest makes into them
//   input.c   keyboard, mouse and game pads -> the console's controller,
//             a USB pad, a USB keyboard and mouse
//   psg.c     the SN76489A; audio.c its timing, the speakers, the WAV
//   card.c    the SD card: a folder (a FAT32 volume made from it) or an image
//   compat/   what MSVC and a machine without zlib lack
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gamepad.h"

#define SIM_W 256
#define SIM_H 192
#define PSG_CLOCK 3579545.0
#define SIM_RATE 48000                  // audio samples a second

// ---- options -----------------------------------------------------------------------
typedef struct {
    bool headless;                      // no window, no sound: the test mode
    bool strict;                        // stop at the first rule broken
    bool mute;
    bool tv;                            // the TV's shape: border, 4:3
    int scale;                          // window: pixels a console pixel
    int64_t end_us;                     // the run ends here (-1: never)
    const char *script;                 // host.c's PC_SCRIPT language
    const char *out;                    // log.txt, lists.bin, screen.png
    const char *shot;                   // --shot: the last picture, as a PNG
    const char *wav;                    // --wav: the sound, as a WAV
    const char *shots;                  // F12's folder
    const char *card;                   // a folder, or a disk image
    const char *open;                   // the file the menu "picked", a path on the card
    const char *id;                     // the plugin's ID, for the launch block
    const char *name;                   // the window's title
    bool no_launch;                     // started without the menu's launch block
    bool no_psram;
    bool lists;                         // keep lists.bin (headless: on)
} sim_opts_t;

extern sim_opts_t opt;

// ---- model.c: the models, as the rest sees them ---------------------------------------
void model_init(void);                  // host.c's main(), with opt
void model_run(void);                   // the plugin's main(); doesn't return
void model_picture(uint8_t pic[SIM_H][SIM_W]);   // the TV now (vdp_rgb's colours)
void model_console(uint8_t held, int key);       // controller 1: WIRED_* bits, key -1 or 0-11
void model_usb_pad(bool in, const gamepad_t *p); // USB pad 0
void model_usb_key(uint8_t usage, bool down);    // a USB keyboard's key (HID usage)
void model_usb_mouse(int dx, int dy, uint8_t buttons);
void model_end(void);                   // RESET: the session ends (doesn't return)
const char *model_note(void);           // the plugin's note for the menu
int model_fails(void);                  // rules broken so far
void model_png(const char *path, uint8_t pic[SIM_H][SIM_W]);   // a 256x192 PNG, host.c's
uint8_t model_backdrop(void);           // register 7's backdrop colour
extern const uint8_t vdp_rgb[16][3];

// ---- main.c: what the models call --------------------------------------------------------
void sim_on_frame(uint64_t t_us);       // a TV frame starting at t_us has run
void sim_on_fail(void);
void sim_on_end(int fails);             // the session is over; may not return

// ---- audio.c -------------------------------------------------------------------------
typedef struct {
    uint16_t period[3];
    uint8_t vol[4], latch, noise, out[3], nflip;
    int count[4];
    uint16_t lfsr;
    double ticks_per_sample, frac;
    float level[16], last_x, last_y;
} psg_t;
void psg_init(psg_t *p, int rate);
void psg_write(psg_t *p, uint8_t b);
void psg_render(psg_t *p, int16_t *out, int n);

void audio_open(void);                  // SDL audio (live), the WAV (--wav)
void audio_write(double t_us, uint8_t b);   // the console writes b to the PSG at t_us
void audio_frame(uint64_t t_us);        // render up to t_us: speakers, WAV
void audio_pace(void);                  // live: wait while a few frames are queued
bool audio_live(void);                  // the speakers set the pace
void audio_close(void);

// ---- input.c -------------------------------------------------------------------------
enum { ROLE_CONSOLE, ROLE_USB_PAD, ROLE_USB_KEYBOARD, ROLE_COUNT };
typedef struct {
    bool quit, reset, shot, role_next, help;   // the simulator's own keys, pressed this frame
} sim_keys_t;
void input_init(void *window);          // live: game pads
void input_poll(sim_keys_t *k);         // events -> the models; the simulator's keys
int input_role(void);
const char *input_role_name(void);
void input_set_role(int role);
bool input_start_pressed(void);         // RESET's screen: fire, Enter, Space, a pad's A or Start

// ---- card.c --------------------------------------------------------------------------
bool card_open(const char *path);       // a folder or a disk image; false: an error, said
bool card_present(void);
uint32_t card_mb(void);
int64_t card_pread(int fd, void *buf, size_t n, int64_t off);
int64_t card_pwrite(int fd, const void *buf, size_t n, int64_t off);
int card_fopen(const char *path, int flags);   // (host.c's own main opens PC_CARD with it)
void card_sync(void);                   // a folder's changes back into it
char *card_resolve_open(const char *open, const char **card);   // --open as a path on the card
