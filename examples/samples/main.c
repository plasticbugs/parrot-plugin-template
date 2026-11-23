// Samples example: sound as samples, about 6,000 a second, through the
// sound chip used as a DAC, with lists that run back to back.
//
// The sound chip has no DAC, but three of its tones parked at a period of
// 1 sit at a level set by their volumes, and the three summed make 512
// levels (dac3.h, made by tools/mkdac3.py from levels measured on a real
// console). A sample is three volume writes, one for each tone.
//
// The samples must come at a steady rate, so their timing is the list's:
// a list here holds 100 of them, evenly spaced, with runs of the picture
// in between to fill the time. Each list is a little longer than a TV
// frame, and the console goes from one list straight into the next, with
// no wait for the frame's start. Then a list's length is the sample clock.
// That's what BACK_TO_BACK in CMakeLists.txt tells the models: they check
// that every list is long enough by MAME's costs, since a list that falls
// short makes the console wait for the next frame, a gap in the sound.
//
// Two rules come with it. A list doesn't start in the vertical blank any
// more, so the blank part stays empty: every video write goes in the
// second part, at 31 cycles a byte. And with nothing new, the console runs
// the last list again, so everything in a list must be safe to run twice.
//
// Keypad 1-8 plays a note, fire a drum. The picture is the sound's wave.
#include <math.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvdiag.h"
#include "cvframes.h"
#include "cvinput.h"            // cvin_decode_key()
#include "dac3.h"               // made by tools/mkdac3.py: level -> three attenuations
#include "font8.h"              // the menu's 8x8 font, ASCII 32-95
#include "gbkernel.h"           // made by the build: the frame kernel
#include "handoff.h"

// ---- the list's timing ---------------------------------------------------------------
// What each command costs in Z80 cycles, from gbkernel.asm's header
#define RUN_T(n)    (106 + 31 * (n))    // a video run, in the second part
#define PSG_T(n)    (75 + 52 * (n))
#define END_T       54                  // the blank part's end (the list's own end is 52)
#define LIST_END_T  52
// Back to back, a list's period is its commands plus about 460 cycles of
// the kernel's own (354 from a list's end to the next list's first command,
// 105 for the frame's NMI). LIST_T is the commands: 59,700 cycles by the
// header's costs, which run a little over MAME's (a sound byte is 51 there,
// 52 here), so the list is still longer than a frame (59,736) in MAME.
#define SAMPLES     100                 // a list
#define LIST_T      59700
#define PERIOD_T    (LIST_T + 354 + 105)
#define Z80_HZ      3579545
#define RATE        ((float)SAMPLES * Z80_HZ / PERIOD_T)    // about 5,950 a second
#define FIRST_T     END_T               // the first sample: right after the empty blank part
#define STEP_T      ((LIST_T - FIRST_T - PSG_T(10) - LIST_END_T) / (SAMPLES - 1))

// ---- the picture ---------------------------------------------------------------------
// Graphics II as a bitmap (examples/frames): patterns at $0000, colours at
// $2000, 6144 bytes each. Text at the top, the wave in a band below it.
#define TABLE   6144
#define PATTERN 0x0000
#define COLOUR  0x2000
#define NAMES   0x1800
#define WAVE_Y  88                      // the wave's middle line
#define WAVE_H  40                      // its height either way
static uint8_t want[2 * TABLE], shown[2 * TABLE];
static uint32_t cursor;                 // where the search for changed bytes got to
static uint32_t refresh;                // where the filler got to
static bool dirty;                      // want[] may differ from shown[]

static void text(int col, int row, const char *s) {
    for (; *s; s++, col++)
        for (int r = 0; r < 8; r++) {
            const int c = *s >= 32 && *s < 96 ? *s : '?';
            want[(row * 32 + col) * 8 + r] = font8[c - 32][r];
        }
}

static void picture_start(void) {
    memset(want + TABLE, 0xF1, TABLE);              // white on black
    for (int i = 0; i < 2 * TABLE; i++) shown[i] = (uint8_t)~want[i];   // the console's: unknown, so sent
    text(2, 1, "SAMPLES EXAMPLE");
    text(2, 3, "KEYPAD 1-8: A NOTE");
    text(2, 4, "FIRE: A DRUM");
    text(2, 20, "100 SAMPLES A LIST, AND THE");
    text(2, 21, "LISTS BACK TO BACK");
    for (int x = 16; x < 240; x++) want[(WAVE_Y / 8 * 32 + x / 8) * 8 + WAVE_Y % 8] |= (uint8_t)(0x80 >> (x & 7));
    for (int i = (WAVE_Y - WAVE_H) / 8 * 256; i < (WAVE_Y + WAVE_H + 8) / 8 * 256; i++)
        want[TABLE + i] = 0x31;                     // the wave in light green
}

// the last list's samples as a line of dots, two pixels apart
static void picture(const int16_t *s) {
    dirty = true;
    for (int y = WAVE_Y - WAVE_H; y <= WAVE_Y + WAVE_H; y++)
        for (int tx = 2; tx < 30; tx++) want[(y / 8 * 32 + tx) * 8 + (y & 7)] = y == WAVE_Y ? 0xFF : 0;
    for (int k = 0; k < SAMPLES; k++) {
        const int x = 28 + 2 * k, y = WAVE_Y - s[k] * WAVE_H / 32768;
        want[(y / 8 * 32 + x / 8) * 8 + (y & 7)] |= (uint8_t)(0x80 >> (x & 7));
    }
}

// ---- the sound -----------------------------------------------------------------------
static float sine[256];
static uint32_t note_phase, note_step;
static float note_env, drum_env;
static uint32_t noise = 1;

// a list's samples, -32768..32767: the note, a decaying sine, and the
// drum, decaying noise
static void make_samples(int16_t *s) {
    for (int k = 0; k < SAMPLES; k++) {
        note_phase += note_step;
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        const float v = sine[note_phase >> 24] * note_env + ((int32_t)noise >> 16) / 32768.0f * drum_env;
        note_env *= 0.99985f;                       // about a second
        drum_env *= 0.9990f;                        // about 0.15 s
        const float c = v > 1 ? 1 : v < -1 ? -1 : v;
        s[k] = (int16_t)(c * 32767);
    }
}

// a sample as its three volume writes: the DAC's middle is level 256
static void sample_bytes(int16_t v, uint8_t *b) {
    int level = 256 + v / 128;
    level = level < 0 ? 0 : level > EXDAC3_LEVELS - 1 ? EXDAC3_LEVELS - 1 : level;
    const uint16_t a = ex_dac3[level];
    b[0] = (uint8_t)(0x90 | a >> 8);
    b[1] = (uint8_t)(0xB0 | (a >> 4 & 15));
    b[2] = (uint8_t)(0xD0 | (a & 15));
}

// ---- lists ---------------------------------------------------------------------------
static uint8_t list[CVF_LIST_MAX];
static size_t len;
static uint32_t cost;                   // cycles from the list's first command

static void run(uint32_t i, uint32_t n) {
    const uint16_t address = (uint16_t)(i < TABLE ? PATTERN + i : COLOUR + i - TABLE);
    list[len++] = 1;
    list[len++] = (uint8_t)address;
    list[len++] = (uint8_t)(address >> 8 | 0x40);
    list[len++] = (uint8_t)n;
    memcpy(list + len, want + i, n);
    memcpy(shown + i, want + i, n);                 // it will be so, once this list has run
    len += n;
    cost += RUN_T(n);
}

// fill the time until `until`: the picture's changed bytes, or else bytes
// the console already has (safe to send again), so the time is used either way
static void fill(uint32_t until) {
    while (cost + RUN_T(1) <= until) {
        uint32_t n = (until - cost - RUN_T(0)) / 31;
        if (n > 255) n = 255;
        uint32_t scanned = 0;
        while (dirty && scanned < 2 * TABLE && want[cursor] == shown[cursor]) {
            cursor = (cursor + 1) % (2 * TABLE);
            scanned++;
        }
        if (scanned == 2 * TABLE) dirty = false;   // all sent: no need to look again until picture()
        uint32_t i;
        if (dirty) {
            i = cursor;                             // a run of the picture from here
        } else {
            i = refresh;                            // nothing changed: the same again
            refresh = (refresh + n) % (2 * TABLE);
        }
        const uint32_t end = i < TABLE ? TABLE : 2 * TABLE;
        if (i + n > end) n = end - i;
        run(i, n);
        if (i == cursor) cursor = (i + n) % (2 * TABLE);
    }
}

static void psg(const uint8_t *b, uint8_t n) {
    list[len++] = 2;
    list[len++] = n;
    memcpy(list + len, b, n);
    len += n;
    cost += PSG_T(n);
}

static void sample_list(const int16_t *s) {
    len = 0;
    cost = 0;
    list[len++] = 0;                                // the blank part: empty
    cost += END_T;
    for (int k = 0; k < SAMPLES; k++) {
        fill(FIRST_T + (uint32_t)k * STEP_T);
        uint8_t b[10];
        sample_bytes(s[k], b);
        if (k == 0) {
            // Once a list, the rest of the chip: the three tones parked at
            // a period of 1, the noise off. A byte the console misreads
            // then lasts a list at most.
            static const uint8_t park[7] = {0x81, 0x00, 0xA1, 0x00, 0xC1, 0x00, 0xFF};
            memcpy(b + 3, park, sizeof park);
            psg(b, 10);
        } else {
            psg(b, 3);
        }
    }
    fill(LIST_T - LIST_END_T);
    list[len++] = 0;
    cost += LIST_END_T;
}

// The first list: the names (tile i at place i, in each third), so that the
// tables are a bitmap, and the chip quiet. An ordinary list: it fits a frame.
static void first_list(void) {
    static const uint8_t quiet[] = {0x81, 0x00, 0x9F, 0xA1, 0x00, 0xBF, 0xC1, 0x00, 0xDF, 0xFF};
    len = 0;
    list[len++] = 0;
    psg(quiet, sizeof quiet);
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
    cvf_note("Samples example");
    for (int i = 0; i < 256; i++) sine[i] = 0.8f * sinf(6.2831853f * i / 256);
    picture_start();
    dirty = true;

    static const float notes[8] = {261.63f, 293.66f, 329.63f, 349.23f, 392.00f, 440.00f, 493.88f, 523.25f};
    static int16_t s[SAMPLES];
    first_list();
    bool pending = true;
    uint32_t seen = 0;
    uint8_t last_key = 15, last_fire = 3;
    for (;;) {
        cvf_task(time_us_32());
        if (pending && cvf_publish(list, len)) pending = false;
        // Back to back, cvf_frames() counts the lists the console starts,
        // not TV frames: a list starts, make the next. Have it ready before
        // this one ends, or the console plays this one again.
        if (pending || cvf_frames() == seen) continue;
        seen = cvf_frames();

        uint8_t joy, fire, key;
        cvf_input(&joy, &fire, &key);
        (void)joy;
        const int k = key == last_key ? -1 : cvin_decode_key(key);    // a key going down
        if (k >= 1 && k <= 8) {
            note_step = (uint32_t)(notes[k - 1] / RATE * 4294967296.0f);
            note_env = 1;
        }
        if ((fire & 3) != 3 && last_fire == 3) drum_env = 0.7f;
        last_key = key;
        last_fire = fire & 3;

        picture(s);                                 // the samples the last list played
        make_samples(s);
        sample_list(s);
        pending = true;
    }
}
