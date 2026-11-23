// tms9918_rgb.h -- the TMS9918A's 16 colours as RGB, for a plugin's own
// previews on the computer: a test that writes a PNG of what the console
// would show, or a colour map that finds each arcade colour's nearest.
// The same values the simulator and the models draw with (the SDK's
// midihello/vdp.c, vdp_rgb) and tools/png256.py compares with, so a
// preview made with these matches their pictures exactly. Colour 0 is
// transparent, black on the screen.
#pragma once
#include <stdint.h>

static const uint8_t tms9918_rgb[16][3] = {
    {0, 0, 0},       {0, 0, 0},       {33, 200, 66},   {94, 220, 120},  // transparent, black, medium green, light green
    {84, 85, 237},   {125, 118, 252}, {212, 82, 77},   {66, 235, 245},  // dark blue, light blue, dark red, cyan
    {252, 85, 84},   {255, 121, 120}, {212, 193, 84},  {230, 206, 128}, // medium red, light red, dark yellow, light yellow
    {33, 176, 59},   {201, 91, 186},  {204, 204, 204}, {255, 255, 255}, // dark green, magenta, grey, white
};
