// lib/mcs48/test.c -- a smoke test of the MCS-48 core: small hand-made
// programs (no ROM bytes from anywhere), their results and cycle counts as
// MAME's 8035 gives them, run in one slice and in slices of every size.
// Exits 1 on a failure. Build and run (README.md):
//   cc -std=c11 -O1 -Wall -Wextra -Ilib lib/mcs48/test.c -o /tmp/mcs48test && /tmp/mcs48test
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "mcs48/mcs48.h"

static uint8_t rom[0x1000];
static char evlog[4096];
static size_t evlen;
static int fails;
static uint8_t ext[256], bus = 0x5a, p2_pins = 0xff;
static int abort_on_p1, wait_on_bus;
static int t1_mode;             // 0: T1 low; 1: T1 = bit 2 of the cycle count

static void ev(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(evlog + evlen, sizeof evlog - evlen, fmt, ap);
    va_end(ap);
    if (n > 0 && evlen + (size_t)n < sizeof evlog) evlen += (size_t)n;
}

static int t1_at(uint64_t c) { return t1_mode ? (int)((c >> 2) & 1) : 0; }

static void p1_w(mcs48_t *m, uint8_t d) {
    ev("P1=%02X@%" PRIu64 " ", d, mcs48_total(m));
    if (abort_on_p1) mcs48_abort(m);
}
static void p2_w(mcs48_t *m, uint8_t d) { ev("P2=%02X@%" PRIu64 " ", d, mcs48_total(m)); }
static uint8_t bus_r(mcs48_t *m) {
    if (wait_on_bus) mcs48_adjust_icount(m, -wait_on_bus);
    return bus;
}

// the default copy: mcs48_run, mcs48_reset, mcs48_burn
#define MCS48_ROM         rom
#define MCS48_P1_W(d)     p1_w(m, d)
#define MCS48_P2_W(d)     p2_w(m, d)
#define MCS48_P2_R()      p2_pins
#define MCS48_BUS_R()     bus_r(m)
#define MCS48_BUS_W(d)    ev("BUS=%02X ", (unsigned)(d))
#define MCS48_PROG_W(s)   ev("PROG%d ", (int)(s))
#define MCS48_EXT_R(a)    ext[a]
#define MCS48_EXT_W(a, d) (ext[a] = (uint8_t)(d))
#define MCS48_T1()        t1_at(mcs48_total(m))
#include "mcs48/mcs48.c"

// a second copy in the same file, private, with a hook: hk_run ...
static int hook_calls, hook_skips;
static int hk_hook(mcs48_t *m);
#define MCS48_NAME(x)     hk_##x
#define MCS48_API         static
#define MCS48_ROM         rom
#define MCS48_P1_W(d)     p1_w(m, d)
#define MCS48_HOOK(m)     hk_hook(m)
#define MCS48_HOOK_AT(pc) ((pc) >= 0x100)
#include "mcs48/mcs48.c"

// at $105, a loop of 10 NOPs and a JMP back ($105-$110), 12 cycles a turn,
// that the hook runs a turn at a time while a turn fits
static int hk_hook(mcs48_t *m) {
    hook_calls++;
    if (m->pc != 0x105 || m->icount <= 12) return 0;
    while (m->icount > 12) { hk_burn(m, 12); hook_skips++; }
    return 1;
}

#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void load(unsigned at, const uint8_t *b, size_t n) { memcpy(rom + at, b, n); }

static void start(mcs48_t *m) {
    mcs48_init(m);
    evlen = 0;
    evlog[0] = 0;
    mcs48_reset(m);
    evlen = 0;
    evlog[0] = 0;
}

// run until the total reaches `until`, in slices of `slice` (0: one slice)
static void run_to(mcs48_t *m, uint64_t until, int slice) {
    while (m->total < until) {
        int c = (int)(until - m->total);
        if (slice && c > slice) c = slice;
        int ran = mcs48_run(m, c);
        if (ran <= 0) { fails++; printf("FAIL: a slice ran %d\n", ran); return; }
    }
}

// the arithmetic, a call and its stack, ports, and every cycle
static void test_basics(void) {
    memset(rom, 0, sizeof rom);
    static const uint8_t p[] = {
        0x23, 0x3c,         // 000 mov a,#3c        2 (the cycles at its end)
        0x03, 0xc8,         // 002 add a,#c8        4   a=04 cy ac
        0xa8,               // 004 mov r0,a         5
        0x23, 0x15,         // 005 mov a,#15        7
        0x13, 0x27,         // 007 addc a,#27       9   a=3d (carry in)
        0x57,               // 009 da a             10  a=43
        0x39,               // 00a outl p1,a        12
        0x14, 0x50,         // 00b call 050         14
        0xf5,               // 00d sel mb1          22
        0x04, 0x20,         // 00e jmp 820          24
    };
    load(0, p, sizeof p);
    static const uint8_t sub[] = {
        0xc7,               // 050 mov a,psw        15  09: sp 1
        0x3a,               // 051 outl p2,a        17
        0xf8,               // 052 mov a,r0         18
        0x37,               // 053 cpl a            19  fb
        0x83,               // 054 ret              21
    };
    load(0x50, sub, sizeof sub);
    static const uint8_t hi[] = {0x23, 0x77, 0x39, 0x04, 0x25};   // 820 mov a,#77; outl p1,a @28; jmp 825 (mb1)
    load(0x820, hi, sizeof hi);
    static const uint8_t spin[] = {0x04, 0x25};                   // 825 jmp 825
    load(0x825, spin, sizeof spin);
    const char *want = "P1=43@12 P2=09@17 P1=77@28 ";
    for (int slice = 0; slice <= 5; slice++) {
        mcs48_t m;
        start(&m);
        run_to(&m, 40, slice);
        CHECK(!strcmp(evlog, want), "slice %d: %s, want %s", slice, evlog, want);
        CHECK(m.ram[0] == 0x04, "r0 %02X", m.ram[0]);
        CHECK(m.ram[8] == 0x0d && m.ram[9] == 0x00, "stack %02X %02X", m.ram[8], m.ram[9]);
        CHECK(m.a == 0x77 && (m.pc & 0xfff) == 0x825 && (m.psw & 7) == 0, "a %02X pc %03X psw %02X", m.a, m.pc, m.psw);
    }
    // one slice of 11 cycles overruns by one (the OUTL from 10 to 12)
    mcs48_t m;
    start(&m);
    int ran = mcs48_run(&m, 11);
    CHECK(ran == 12 && m.total == 12 && m.pc == 0x00b, "ran %d total %" PRIu64 " pc %03X", ran, m.total, m.pc);
}

// the timer, its interrupt and RETR, in every size of slice
static void test_timer(void) {
    memset(rom, 0, sizeof rom);
    static const uint8_t p[] = {
        0x04, 0x10,         // 000 jmp 010          2
        0, 0, 0, 0, 0,
        0x04, 0x40,         // 007 jmp 040 (the timer's interrupt)
    };
    load(0, p, sizeof p);
    static const uint8_t main_[] = {
        0x23, 0xfe,         // 010 mov a,#fe        4
        0x62,               // 012 mov t,a          5
        0x25,               // 013 en tcnti         6
        0x55,               // 014 strt t           7: prescaler 0
        0x04, 0x15,         // 015 jmp 015          the 32nd turn ends at 71: overflow
    };
    load(0x10, main_, sizeof main_);
    static const uint8_t isr[] = {
        0x23, 0xaa,         // 040 mov a,#aa        (entry 73, jmp 75) 77
        0x39,               // 042 outl p1,a        79
        0x42,               // 043 mov a,t          80
        0x3a,               // 044 outl p2,a        82: the timer, 00
        0x65,               // 045 stop tcnt        83
        0x93,               // 046 retr             85
    };
    load(0x40, isr, sizeof isr);
    const char *want = "P1=AA@79 P2=00@82 ";
    for (int slice = 0; slice <= 7; slice++) {
        mcs48_t m;
        start(&m);
        run_to(&m, 100, slice);
        CHECK(!strcmp(evlog, want), "slice %d: %s, want %s", slice, evlog, want);
        CHECK(m.timer == 0 && m.prescaler == 76 - 64 && m.timer_flag && !m.irq_in_progress,
              "slice %d: timer %02X prescaler %d flag %d", slice, m.timer, m.prescaler, m.timer_flag);
        CHECK(m.ram[8] == 0x15 && m.ram[9] == 0x00, "slice %d: stack %02X %02X", slice, m.ram[8], m.ram[9]);
    }
}

// the INT pin, and MAME's hack: a JNI not taken at a slice's end is taken
// before the interrupt that comes between slices
static void test_irq(void) {
    memset(rom, 0, sizeof rom);
    static const uint8_t p[] = {
        0x04, 0x10,         // 000 jmp 010
        0x00,
        0x04, 0x30,         // 003 jmp 030
    };
    load(0, p, sizeof p);
    static const uint8_t main_[] = {
        0x05,               // 010 en i
        0x86, 0x20,         // 011 jni 020
        0x04, 0x11,         // 013 jmp 011
    };
    load(0x10, main_, sizeof main_);
    static const uint8_t got[] = {0x23, 0x01, 0x39, 0x04, 0x23};  // 020 mov a,#1; outl p1,a; jmp 023
    load(0x20, got, sizeof got);
    static const uint8_t isr[] = {0x23, 0x02, 0x39, 0x93};        // 030 mov a,#2; outl p1,a; retr
    load(0x30, isr, sizeof isr);

    // after the JNI (cycles 3-5), between slices
    mcs48_t m;
    start(&m);
    CHECK(mcs48_run(&m, 1) == 2 && mcs48_run(&m, 1) == 1 && mcs48_run(&m, 1) == 2, "the slices");
    CHECK(m.pc == 0x013 && m.irq_polled, "pc %03X polled %d", m.pc, m.irq_polled);
    mcs48_set_irq(&m, 1);
    mcs48_run(&m, 1);   // entry (2) and jmp 030 (2)
    CHECK(m.ram[8] == 0x20 && m.pc == 0x030 && m.total == 9, "the JNI's target pushed: %02X, pc %03X, total %" PRIu64,
          m.ram[8], m.pc, m.total);
    mcs48_set_irq(&m, 0);
    run_to(&m, 30, 0);
    CHECK(!strcmp(evlog, "P1=02@13 P1=01@19 "), "%s", evlog);

    // after the JMP (cycles 6-7): the next instruction's address pushed
    start(&m);
    mcs48_run(&m, 7);
    CHECK(m.pc == 0x011 && !m.irq_polled, "pc %03X", m.pc);
    mcs48_set_irq(&m, 1);
    mcs48_run(&m, 1);
    CHECK(m.ram[8] == 0x11 && m.pc == 0x030, "pushed %02X pc %03X", m.ram[8], m.pc);
}

// the counter: a falling edge on T1, sampled once a cycle
static void test_counter(void) {
    memset(rom, 0, sizeof rom);
    static const uint8_t p[] = {
        0x23, 0xfc,         // 000 mov a,#fc        2
        0x62,               // 002 mov t,a          3
        0x45,               // 003 strt cnt         4: T1 sampled at 4
        0x00,               // 004 nop              5
        0x04, 0x04,         // 005 jmp 004
    };
    load(0, p, sizeof p);
    for (int slice = 0; slice <= 3; slice++) {
        t1_mode = 1;
        mcs48_t m;
        start(&m);
        run_to(&m, 61, slice);
        int edges = 0;
        for (uint64_t c = 5; c < m.total; c++) edges += t1_at(c - 1) && !t1_at(c);
        CHECK(m.timer == (uint8_t)(0xfc + edges) && m.timer_flag == (edges >= 4),
              "slice %d: timer %02X after %d edges", slice, m.timer, edges);
        t1_mode = 0;
    }
}

// MOVX, the bus, an 8243's MOVD, wait states and an aborted slice
static void test_io(void) {
    memset(rom, 0, sizeof rom);
    static const uint8_t p[] = {
        0xb8, 0x05,         // 000 mov r0,#05       2
        0x23, 0x99,         // 002 mov a,#99        4
        0x90,               // 004 movx @r0,a       6
        0x27,               // 005 clr a            7
        0x80,               // 006 movx a,@r0       9
        0x3d,               // 007 movd p5,a        11
        0x0c,               // 008 movd a,p4        13: a = p2's pins' low half
        0x02,               // 009 outl bus,a       15
        0x88, 0xf0,         // 00a orl bus,#f0      17
        0x39,               // 00c outl p1,a        19
        0x00,               // 00d nop              20
    };
    load(0, p, sizeof p);
    mcs48_t m;
    start(&m);
    p2_pins = 0xf6;
    run_to(&m, 20, 0);
    CHECK(ext[5] == 0x99, "ext %02X", ext[5]);
    const char *want = "P2=F5@11 PROG0 P2=F9@11 PROG1 P2=F0@13 PROG0 P2=FF@13 PROG1 BUS=06 BUS=FA P1=06@19 ";
    CHECK(!strcmp(evlog, want), "%s, want %s", evlog, want);
    CHECK(m.a == 0x06 && m.p2 == 0xff, "a %02X p2 %02X", m.a, m.p2);

    // the bus read adds 3 wait states; a port write ends the slice
    start(&m);
    wait_on_bus = 3;
    abort_on_p1 = 1;
    int ran = mcs48_run(&m, 100);
    CHECK(ran == 22 && m.pc == 0x00d && m.total == 22, "ran %d pc %03X", ran, m.pc);
    wait_on_bus = abort_on_p1 = 0;
    ran = mcs48_run(&m, 1);
    CHECK(ran == 1 && m.pc == 0x00e, "ran %d pc %03X", ran, m.pc);
}

// a few more instructions' results
static void test_ops(void) {
    memset(rom, 0, sizeof rom);
    static const uint8_t p[] = {
        0xb9, 0x03,         // 000 mov r1,#03
        0x23, 0x81,         // 002 mov a,#81
        0x97,               // 004 clr c
        0x67,               // 005 rrc a            a=40 cy=1
        0xf7,               // 006 rlc a            a=81 cy=0
        0xe7,               // 007 rl a             a=03
        0x47,               // 008 swap a           a=30
        0xb1, 0x5c,         // 009 mov @r1,#5c      ram[3]=5c
        0x31,               // 00b xchd a,@r1       a=3c ram[3]=50
        0xd5,               // 00c sel rb1
        0xb8, 0x07,         // 00d mov r0,#07       ram[24]=07
        0xe8, 0x0f,         // 00f djnz r0,00f      (7 turns, 14 cycles)
        0xc5,               // 011 sel rb0
        0xa3,               // 012 movp a,@a        a=rom[03c]
        0xe3,               // 013 movp3 a,@a
        0x37,               // 014 cpl a
        0x17,               // 015 inc a            a = -a
        0xc6, 0x30,         // 016 jz 030
        0xb5,               // 018 cpl f1
        0x76, 0x40,         // 019 jf1 040
    };
    load(0, p, sizeof p);
    rom[0x03c] = 0x21;              // movp: from this page
    rom[0x321] = 0x00;              // movp3: from page 3 -> a = 0, jz taken
    static const uint8_t z[] = {0xb5, 0x76, 0x40};                // 030 cpl f1; jf1 040
    load(0x30, z, sizeof z);
    static const uint8_t j[] = {0x23, 0x42, 0xb3};                // 040 mov a,#42; jmpp @a: pc = rom[042]
    load(0x40, j, sizeof j);        // (its own byte: pc = 0b3)
    static const uint8_t e[] = {0x23, 0x55, 0x39};                // 0b3 mov a,#55; outl p1,a
    load(0xb3, e, sizeof e);
    mcs48_t m;
    start(&m);
    run_to(&m, 49, 0);
    CHECK(m.ram[3] == 0x50 && m.ram[24] == 0 && m.f1 && m.a == 0x55, "ram3 %02X r0' %02X f1 %d a %02X", m.ram[3], m.ram[24], m.f1, m.a);
    CHECK(!strcmp(evlog, "P1=55@49 "), "%s", evlog);
}

// the hook: a copy of its own, a fast path that runs a loop's turns itself
static void test_hook(void) {
    memset(rom, 0, sizeof rom);
    rom[0x10f] = 0x24; rom[0x110] = 0x05;                          // 100: nops; 10f jmp 105
    rom[0] = 0x24; rom[1] = 0x00;                                  // 000 jmp 100
    mcs48_t a, b;
    start(&a);
    start(&b);
    for (int i = 0; i < 50; i++) {
        mcs48_run(&a, 37);
        hk_run(&b, 37);
    }
    CHECK(a.total == b.total && a.pc == b.pc, "total %" PRIu64 "/%" PRIu64 " pc %03X/%03X", a.total, b.total, a.pc, b.pc);
    CHECK(hook_skips > 0 && hook_calls > hook_skips, "hook calls %d skips %d", hook_calls, hook_skips);
}

int main(void) {
    test_basics();
    test_timer();
    test_irq();
    test_counter();
    test_io();
    test_ops();
    test_hook();
    if (fails) { printf("mcs48: %d failed\n", fails); return 1; }
    printf("mcs48: all passed\n");
    return 0;
}
