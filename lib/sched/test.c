// test.c -- lib/sched's smoke test: two toy CPUs with MAME's execute_run
// semantics (instructions of one cycle; A at 1 MHz, B at 2 MHz), a 10 kHz
// and a 50 Hz timer, and checks of what MAME's scheduler does with them:
// the slices each CPU is given (to the next timer, the quantum, the
// target moved back to a CPU that stopped early), a handler that writes a
// latch through synchronize (ending its slice; the other CPU caught up to
// it), the perfect quantum that write asks for (1 us slices for 250 us,
// until the timeslice after it ends), an IRQ line set with HOLD_LINE and
// let go at the acknowledge, a RESET line (the CPU's cycles eaten while
// held, then its reset), the epoch moving on, and a time the board keeps.
//
//   cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-missing-field-initializers lib/sched/test.c -o schedtest && ./schedtest
#include <stdio.h>
#include <string.h>

#include "sched.h"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- the toy CPUs: <cpu>_run, _abort, _done as sched.h asks ------------------------------
typedef struct {
	int icount, cycles_running, cycles_stolen;
	uint64_t total;                     // cycles since its reset
	int irq;                            // its IRQ line as set
	int resets;
} toy_t;
static toy_t toy[2];
static int req[2][1024], nreq[2];       // the cycles each run was given
enum { A, B };
enum { L_IRQ, L_RESET };                // B's lines
static void cpu_step(int d, uint64_t cycle);

static int toy_run(int d, int cycles) {
	toy_t *t = &toy[d];
	if (nreq[d] < 1024) req[d][nreq[d]++] = cycles;
	t->cycles_running = cycles;
	t->cycles_stolen = 0;
	t->icount = cycles;
	while (t->icount > 0) {
		cpu_step(d, t->total + (uint64_t)(t->cycles_running - t->icount));   // an instruction starts
		t->icount -= 1;
	}
	int ran = cycles - t->icount - t->cycles_stolen;
	t->total += (uint64_t)ran;
	return ran;
}
static void toy_abort(int d) {
	toy_t *t = &toy[d];
	if (t->icount > 0) {
		t->cycles_stolen += t->icount;
		t->cycles_running -= t->icount;
		t->icount = 0;
	}
}
static int toy_done(int d) { return toy[d].cycles_running - toy[d].icount; }
static void toy_reset(int d) { toy[d].total = 0; toy[d].irq = 0; toy[d].resets++; }

#define SCHED_CPUS 2
#define SCHED_RUN(d, n) toy_run(d, n)
#define SCHED_ABORT(d) toy_abort(d)
#define SCHED_DONE(d) toy_done(d)
#define SCHED_RESET(d) toy_reset(d)
#include "sched.c"

// ---- the board -----------------------------------------------------------------------------
static int t10k, t50;
static int latch = -1;
static sched_time_t latch_at;
static uint64_t latch_a, latch_b;       // the CPUs' totals as the latch is written
static int acked_vector = -1;
static uint64_t acked_at;
static uint64_t a_at[4], b_at[4];       // the totals at the first 50 Hz ticks
static int n50;
static sched_time_t vbl;                // kept: the last 50 Hz tick

static void set_irq_b(int state) { toy[B].irq = state; }
static void cb_latch(int param) {       // the latch's write, synchronized
	latch = param;
	latch_at = sched_now();
	latch_a = sched_total_cycles(A);
	latch_b = sched_total_cycles(B);
	sched_set_input(L_IRQ, SCHED_HOLD_LINE);
}
static void cb_10k(int param) { (void)param; }
static void cb_50(int param) {
	(void)param;
	vbl = sched_now();
	if (n50 < 4) {
		a_at[n50] = sched_total_cycles(A);
		b_at[n50] = sched_total_cycles(B);
	}
	n50++;
	if (n50 == 1) sched_set_input(L_RESET, SCHED_ASSERT_LINE);
	if (n50 == 2) sched_set_input(L_RESET, SCHED_CLEAR_LINE);
}
static void cpu_step(int d, uint64_t cycle) {
	if (d == A && cycle == 150) {       // A writes the latch: a synchronize, and the perfect quantum
		CHECK(sched_now() == SCHED_USEC(150), "A's time at its cycle 150: %lld", (long long)sched_now());
		sched_synchronize(cb_latch, 42);
		sched_perfect_quantum(SCHED_USEC(250));
	}
	if (d == B && toy[B].irq && acked_vector < 0) {     // B takes the interrupt
		acked_at = cycle;
		acked_vector = sched_irq_ack(L_IRQ);
	}
}

int main(void) {
	sched_init();
	sched_cpu(A, 1000000, 1, 1);
	sched_cpu(B, 2000000, 1, 1);
	sched_line(L_IRQ, B, SCHED_LINE_NORMAL, 0xff, set_irq_b);
	sched_line(L_RESET, B, SCHED_LINE_RESET, 0, NULL);
	t10k = sched_timer_alloc(cb_10k);
	t50 = sched_timer_alloc(cb_50);
	sched_timer_adjust(t10k, SCHED_HZ(10000), 0, SCHED_HZ(10000));
	sched_timer_adjust(t50, SCHED_HZ(50), 0, SCHED_HZ(50));
	sched_keep(&vbl);
	for (int d = 0; d < 2; d++) {       // the machine's reset
		sched_reset_cpu(d);
		toy_reset(d);
	}
	toy[A].resets = toy[B].resets = 0;

	sched_run_to(500, 1000000);         // to 500 us
	// slice 1 to the 10 kHz timer; slice 2 to 200 us, A stopped after its
	// cycle 150 (151 run), B to there; then 1 us slices to 400 us (the
	// quantum's 250 us from 150, kept to the timeslice that starts at 400)
	CHECK(latch == 42 && latch_at == SCHED_USEC(150), "the latch: %d at %lld", latch, (long long)latch_at);
	CHECK(latch_a == 151 && latch_b == 302, "at the latch: A %llu, B %llu cycles; want 151, 302", (unsigned long long)latch_a,
	      (unsigned long long)latch_b);
	CHECK(nreq[A] >= 252 && req[A][0] == 100 && req[A][1] == 100, "A's first slices: %d, %d", req[A][0], req[A][1]);
	CHECK(req[B][0] == 200 && req[B][1] == 102, "B's first slices: %d, %d; want 200, 102", req[B][0], req[B][1]);
	int perfect = 0;
	while (2 + perfect < nreq[B] && req[B][2 + perfect] == 2) perfect++;
	CHECK(perfect == 249, "B's 1 us slices: %d, want 249", perfect);
	CHECK(2 + perfect < nreq[B] && req[B][2 + perfect] == 200, "B's slice after the perfect quantum: %d, want 200",
	      req[B][2 + perfect]);
	CHECK(acked_vector == 0xff && acked_at == 302 && toy[B].irq == 0 && sched_input_state(L_IRQ) == SCHED_CLEAR_LINE,
	      "the IRQ: vector %02x at B's cycle %llu, line %d", acked_vector, (unsigned long long)acked_at, toy[B].irq);

	sched_run_to(60000, 1000000);       // to 60 ms
	CHECK(n50 == 3, "50 Hz ticks: %d", n50);
	CHECK(a_at[0] == 20000 && b_at[0] == 40000, "at 20 ms: A %llu, B %llu", (unsigned long long)a_at[0],
	      (unsigned long long)b_at[0]);
	CHECK(b_at[1] == 80000, "at 40 ms, B's total with the cycles eaten in reset: %llu, want 80000", (unsigned long long)b_at[1]);
	CHECK(toy[B].resets == 1 && b_at[2] == 40000 && a_at[2] == 60000, "B's reset at 40 ms: %d resets, B %llu at 60 ms",
	      toy[B].resets, (unsigned long long)b_at[2]);

	sched_run_to(2500000, 1000000);     // to 2.5 s, over the epoch twice
	CHECK(sched_epoch() == 2, "the epoch: %u", (unsigned)sched_epoch());
	CHECK(sched_total_cycles(A) == 2500000, "A at 2.5 s: %llu", (unsigned long long)sched_total_cycles(A));
	CHECK(sched_total_cycles(B) == 4920000, "B at 2.5 s: %llu", (unsigned long long)sched_total_cycles(B));
	CHECK(sched_ticks(sched_now(), 1000) == 2500, "now: %llu ms", (unsigned long long)sched_ticks(sched_now(), 1000));
	CHECK(sched_ticks(vbl, 50) == 125 && n50 == 125, "the kept time: tick %llu of 50 Hz (%d ticks)",
	      (unsigned long long)sched_ticks(vbl, 50), n50);

	if (fails) {
		printf("lib/sched: %d failed\n", fails);
		return 1;
	}
	printf("lib/sched: ok\n");
	return 0;
}
