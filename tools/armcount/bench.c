// SPDX-License-Identifier: MIT
// tools/armcount/bench.c -- a bench for tools/armcount/armcount.py. Copy
// it next to your plugin's tests and put your plugin's work for one list
// in bench_list().
//
// The bench is your plugin's code without the cart: no SDK start, no
// console, no card, no USB. It runs on the M33 model in Unicorn, which
// counts the cycles from one bench_mark() call to the next, so call
// bench_mark() once after each list's work and nothing else between. Data
// from outside (a ROM set the user owns, a script of inputs) goes in a
// global that armcount.py fills before the start: --load SYMBOL=FILE.
//
// This one is a working example: each "list" draws a moving picture of
// 224 x 256 pens and scales it by 3/4 with the SDK's cvscale34(), as an
// arcade plugin does every frame.
//
//   tools/armcount/build.sh /tmp/bench.elf tools/armcount/bench.c sdk/firmware/revc/sdk/cvscale.c
//   ARM_PY=~/armcount-venv/bin/python3
//   $ARM_PY tools/armcount/armcount.py /tmp/bench.elf --lists 120
#include <stdint.h>
#include <string.h>

#include "cvscale.h"

// What armcount.py may fill from a file before the start (--load
// bench_data=FILE): here, nothing needs it. A plugin puts its ROMs here.
uint8_t bench_data[4];

static uint8_t pens[256][224];          // the board's picture, a pen a pixel
static uint8_t scaled[192][168];
static uint32_t frame;

// one list's work: YOUR CODE (run the board for a frame, make the list)
static void bench_list(void) {
    frame++;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 224; x++)
            pens[y][x] = (uint8_t)(((x + frame) >> 3 ^ y >> 3) & 1 ? 7 : (x + y + frame) % 31 == 0 ? 15 : 0);
    cvscale34(&pens[0][0], 224, 224, 256, 0, 224, &scaled[0][0], 168);
}

// armcount.py stops at each call: the cycles since the last are one list's
void __attribute__((noinline)) bench_mark(void) { __asm volatile("" ::: "memory"); }

int main(void) {
    // YOUR CODE: the plugin's start (decode the ROMs from bench_data, reset the board)
    for (;;) {
        bench_list();
        bench_mark();
    }
}

extern uint32_t __stack_top;
void __attribute__((naked, noreturn)) _start(void) {
    __asm volatile("ldr sp, =__stack_top\n bl main\n b .");
}
// what newlib asks of a system without one
void *_sbrk(int n) { (void)n; return (void *)-1; }
