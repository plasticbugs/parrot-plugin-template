// lib/dial's smoke test (the kit's own):
//   cc -std=c11 -Wall -Wextra -I lib -I sdk/firmware/revc/sdk lib/dial/test.c -o /tmp/dial-test && /tmp/dial-test
#include <stdio.h>

#include "dial/dial.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void) {
    dial_t d;
    dial_init(&d);
    // the stick held right: 4 at first, then faster, 10 from 0.3 s on
    int first = dial_step(&d, false, true, 0, 0), m = first;
    for (int i = 0; i < 30; i++) m = dial_step(&d, false, true, 0, 0);
    CHECK(first >= 4 && first < 6 && m == 10, "held right: %d then %d", first, m);
    CHECK(dial_step(&d, false, false, 0, 0) == 0, "let go: still");
    CHECK(dial_step(&d, true, false, 0, 0) < 0, "held left: turns left");
    // both held: neither
    dial_step(&d, false, false, 0, 0);
    CHECK(dial_step(&d, true, true, 0, 0) == 0, "both: still");
    // a pad's stick: in proportion, nothing in the middle
    CHECK(dial_step(&d, false, false, 2047, 0) == 12 && dial_step(&d, false, false, -1024, 0) == -6, "pad stick");
    CHECK(dial_step(&d, false, false, 200, 0) == 0, "pad stick's middle");
    // the mouse: 4 counts a step, the rest kept for next time
    int sum = 0;
    for (int i = 0; i < 10; i++) sum += dial_step(&d, false, false, 0, 3);
    CHECK(sum == 7, "mouse: 30 counts made %d steps", sum);
    CHECK(dial_step(&d, false, false, 0, -10) == -2, "mouse left");
    printf(fails ? "lib/dial: %d FAILED\n" : "lib/dial: ok\n", fails);
    return fails != 0;
}
