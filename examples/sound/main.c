// Sound example: the console's sound chip (an SN76489: three square-wave
// voices and a noise voice), played from the cart.
//
// The plugin keeps the chip's 10 bytes -- tone low, tone high and volume
// for voices 0-2, then the noise volume -- and hands them to cvk_sound()
// whenever they change, and every 100 ms anyway: the console misreads a
// byte from the cart now and then, and a sound register stays wrong until
// it's written again. The console writes them to the chip within a
// millisecond or two. They are the chip's own bytes, so "silence" is
// cvk_silence, not zeros.
//
//   keypad 1-8      a note of the scale, while held (voice 0)
//   stick up/down   the octave
//   left fire       a drum (the noise voice, fading out)
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvscreen.h"
#include "handoff.h"
#include "rom_image.h"
#include "wired.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static wired_t pad;

// ---- the PSG's bytes -------------------------------------------------------------
#define PSG_CLOCK 3579545       // the console's: a tone's pitch is PSG_CLOCK / (32 * n), n 1-1023

// voice ch (0-2) at hz, att 0 (loudest) to 15 (silent). The lowest note is 110 Hz.
static void psg_tone(uint8_t snd[10], int ch, unsigned hz, unsigned att) {
    unsigned n = hz ? PSG_CLOCK / (32 * hz) : 1;
    if (n > 1023) n = 1023;
    snd[3 * ch + 0] = (uint8_t)(0x80 | ch << 5 | (n & 15));     // latch: voice, tone, the low 4 bits
    snd[3 * ch + 1] = (uint8_t)(n >> 4);                        // the high 6
    snd[3 * ch + 2] = (uint8_t)(0x90 | ch << 5 | att);          // latch: voice, volume
}

static void psg_noise_volume(uint8_t snd[10], unsigned att) { snd[9] = (uint8_t)(0xF0 | att); }

// The noise's kind and rate is an 11th byte, sent apart: writing it restarts
// the noise, so it goes out only when asked for (once a drum hit), with the
// next cvk_sound(). white: hiss, else a buzz; rate 0-2 high to low.
static void psg_noise_start(bool white, unsigned rate) {
    static uint8_t seq;
    cvk_mb(MB_NCTL, (uint8_t)(0xE0 | (white ? 4 : 0) | rate));
    cvk_mb(MB_NSEQ, ++seq);
}

static const unsigned SCALE[8] = {262, 294, 330, 349, 392, 440, 494, 523};      // C4 to C5
static const char *const NAME[8] = {"C", "D", "E", "F", "G", "A", "B", "C"};
static int octave = 1;          // 0: an octave down, 2: one up
static int note = -1;           // the note sounding, 0-7
static unsigned drum_att = 15;  // the drum's volume as it fades
static uint8_t snd[10];

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    char s[33];
    cvs_line(c, 16, 8, 224, "SOUND", C_LYELLOW, C_BLACK, 2);
    cvs_line(c, 16, 40, 224, "KEYPAD 1-8  A NOTE", C_WHITE, C_BLACK, 1);
    cvs_line(c, 16, 52, 224, "STICK       OCTAVE UP, DOWN", C_WHITE, C_BLACK, 1);
    cvs_line(c, 16, 64, 224, "LEFT FIRE   A DRUM", C_WHITE, C_BLACK, 1);
    if (note < 0) snprintf(s, sizeof s, "OCTAVE %d", octave + 3);
    else snprintf(s, sizeof s, "%s%d  %u HZ", NAME[note], octave + 3 + (note == 7), SCALE[note] << octave >> 1);
    cvs_line(c, 16, 96, 224, s, C_LGREEN, C_BLACK, 2);
    for (int i = 0; i < 8; i++)                 // the scale, the key held lit
        canvas_fill(c, 16 + 28 * i, 128, 24, 16, i == note ? C_LGREEN : C_DBLUE);
    canvas_fill(c, 16, 152, 8 * (15 - (int)drum_att), 8, C_LRED);              // the drum, fading
    canvas_fill(c, 16 + 8 * (15 - (int)drum_att), 152, 8 * (int)drum_att, 8, C_BLACK);
    cvs_line(c, 16, 176, 224, "RESET: BACK TO THE MENU", C_GRAY, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);            // it leaves the PSG silent
    cvs_init(&screen);
    wired_init(&pad);
    memcpy(snd, cvk_silence, sizeof snd);
    cvk_note("Sound example");

    uint32_t t_screen = 0, t_drum = 0, t_sound = 0;
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

        // the controller -> what should sound now
        const uint8_t hit = wired_pressed(&pad);
        if (hit & WIRED_UP && octave < 2) octave++;
        if (hit & WIRED_DOWN && octave > 0) octave--;
        note = pad.key >= 1 && pad.key <= 8 ? pad.key - 1 : -1;
        if (hit & WIRED_FIRE_L) {
            psg_noise_start(true, 1);
            drum_att = 0;
            t_drum = t;
        }
        if (drum_att < 15 && t - t_drum >= 20000) {     // the drum fades: a step every 20 ms
            drum_att++;
            t_drum = t;
        }

        // ...as the chip's bytes, sent when they change and every 100 ms
        uint8_t next[10];
        memcpy(next, cvk_silence, sizeof next);
        if (note >= 0) psg_tone(next, 0, SCALE[note] << octave >> 1, 2);
        psg_noise_volume(next, drum_att);
        if (memcmp(next, snd, sizeof snd) || t - t_sound >= 100000) {
            memcpy(snd, next, sizeof snd);
            cvk_sound(snd);
            t_sound = t;
        }

        if (t - t_screen >= 20000) {
            t_screen = t;
            const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
            if (n) cvk_list(list, n, screen.out_full, screen.seq);
        }
    }
}
