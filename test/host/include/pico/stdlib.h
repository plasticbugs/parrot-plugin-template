// The Mac's stand-in for the pico-sdk, for test/host (see host.c): just
// what the examples' loops use, and the clock with its alarms and
// repeating timers (pico/time.h).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pico/time.h"              // (C++ plugins include it as they would the pico-sdk's: it has extern "C")
typedef unsigned int uint;
#ifndef MAX                         // (as the pico-sdk's pico/platform/compiler.h)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((b) > (a) ? (a) : (b))
#endif
static inline void tight_loop_contents(void) {}
