// test/vendor/lib/answer.c: stands for someone else's code, with two of
// the habits such code has: a variable it never uses (a warning) and a
// shift of a negative number (undefined in C, so UBSan reports it). Built
// with --vendor, the models and the simulator report neither, as the
// code's build wouldn't.
#include "answer.h"

static volatile int minus = -1;         // (volatile: the shift happens when it runs)

int vendor_answer(int x) {
    int unused;
    return (minus << 4) + 16 + VENDOR_ANSWER + x - x;
}
