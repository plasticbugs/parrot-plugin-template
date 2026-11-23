// dial.h -- an arcade dial or spinner (Star Trek's, Tempest's, Arkanoid's)
// from what the user has. Once a frame it gives the dial's move:
//
//   - the console's stick, or a USB pad's d-pad, held left or right: a few
//     steps a frame at first, more the longer it's held, as MAME's keys do
//   - a USB pad's left stick: in proportion to how far it's pushed
//   - a USB mouse's x: directly, so many counts a step
//
//   static dial_t dial;
//   dial_init(&dial);                   // Star Trek's numbers; change any after
//   once a list:
//   cvf_input(&joy, &fire, &lines);     // low = pressed
//   int move = dial_update(&dial, !(joy & 8), !(joy & 2));   // - left, + right
//
// dial_update() takes the console's stick left and right, and reads USB
// pad 1 when PADS is on and the mouse when MOUSE is on (cvcart_features()
// in CMakeLists.txt). A plugin that reads the pad and mouse itself calls
// dial_step() with them instead.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "cvusb.h"

typedef struct {
    int start, top;             // steps a frame with the stick held: at first, and at most
    int ramp;                   // frames held to go from start to top
    int pad_max;                // steps a frame with a pad's stick pushed all the way
    int pad_dead;               // the stick's middle that counts as still, of GP_STICK_MAX (2047)
    int mouse_counts;           // mouse counts a step
    int held;                   // frames the stick has been held one way
    int32_t mouse_left;         // counts not yet turned into steps
} dial_t;

static inline void dial_init(dial_t *d) {
    d->start = 4;               // Star Trek: 4 a frame, up to MAME's 10 after 0.3 s
    d->top = 10;
    d->ramp = 18;
    d->pad_max = 12;
    d->pad_dead = 300;
    d->mouse_counts = 4;
    d->held = 0;
    d->mouse_left = 0;
}

// The move this frame from: the stick or d-pad held left or right, a pad's
// stick (-2047..2047, 0 if none) and the mouse's x counts since last frame.
static inline int dial_step(dial_t *d, bool left, bool right, int pad_x, int32_t mouse_dx) {
    int move = 0;
    if (left != right) {
        if (d->held < d->ramp) d->held++;
        const int step = d->ramp ? d->start + (d->top - d->start) * d->held / d->ramp : d->top;
        move = left ? -step : step;
    } else {
        d->held = 0;
        if (pad_x > d->pad_dead || pad_x < -d->pad_dead) move = pad_x * d->pad_max / 2047;
    }
    if (d->mouse_counts > 0) {
        d->mouse_left += mouse_dx;
        const int32_t steps = d->mouse_left / d->mouse_counts;
        d->mouse_left -= steps * d->mouse_counts;
        move += (int)steps;
    }
    return move;
}

// dial_step() with the console's stick held left or right, USB pad 1
// (PADS: its d-pad held, or its left stick in proportion) and the mouse
// (MOUSE).
static inline int dial_update(dial_t *d, bool left, bool right) {
    int pad_x = 0;
    int32_t mouse_dx = 0;
#if CVUSB_PADS
    gamepad_t p;
    if (cvusb_pad(0, &p)) {
        left = left || (p.buttons & GP_LEFT);
        right = right || (p.buttons & GP_RIGHT);
        pad_x = p.lx;
    }
#endif
#if CVUSB_MOUSE
    cvmouse_t m;
    if (cvusb_mouse(&m)) mouse_dx = m.dx;
#endif
    return dial_step(d, left, right, pad_x, mouse_dx);
}
