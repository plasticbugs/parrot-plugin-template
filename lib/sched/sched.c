// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// sched.c -- see sched.h. MAME 0.288's device_scheduler (schedule.cpp) and
// the scheduler's half of device_execute_interface (diexec.cpp) in C. From
// the Star Trek plugin's st.c (Parrot Cart's startrek: three CPUs, the
// 60 Hz quantum and a perfect quantum) and Time Pilot's tp.c (timeplt),
// made CPU-agnostic: it drives each CPU through the board's SCHED_RUN,
// SCHED_ABORT and SCHED_DONE.
//
// A template: the board's .c file defines SCHED_CPUS and those macros and
// includes this. (Built alone, without SCHED_CPUS, it is empty.)
#ifdef SCHED_CPUS
#include "sched.h"

#include <string.h>

#ifndef SCHED_TIMERS
#define SCHED_TIMERS 32
#endif
#ifndef SCHED_LINES
#define SCHED_LINES 8
#endif
#ifndef SCHED_QUANTA
#define SCHED_QUANTA 8
#endif
#ifndef SCHED_KEEPS
#define SCHED_KEEPS 8
#endif
#ifndef SCHED_RESET
#define SCHED_RESET(d) ((void)(d))
#endif
#ifndef SCHED_OVERFLOW
#define SCHED_OVERFLOW() __builtin_trap()   // a table too small: SCHED_TIMERS, SCHED_QUANTA or SCHED_KEEPS
#endif
_Static_assert(SCHED_TIMERS <= 32000, "SCHED_TIMERS: under 32000");

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define SCHED_HOT __attribute__((section(".time_critical.sched")))
#else
#define SCHED_HOT
#endif
#define SCHED_INLINE static inline __attribute__((always_inline))

// ---- time ----------------------------------------------------------------------------------
static uint32_t s_epoch;                // the seconds the times count from
static sched_time_t s_base;             // m_basetime

// floor(a * f / 1e18) for 0 <= a < 1e18, without 128-bit sums
static inline uint64_t s_mul_frac(int64_t a, uint32_t f) {
	uint64_t ah = (uint64_t)a / 1000000000u, al = (uint64_t)a % 1000000000u;
	uint64_t x = ah * f, y = al * f;
	uint64_t q1 = x / 1000000000u, r1 = x % 1000000000u;
	return q1 + (r1 * 1000000000u + y) / (uint64_t)SCHED_SEC;
}

uint64_t sched_ticks(sched_time_t t, uint32_t hz) {
	int64_t s = s_epoch;
	while (t >= SCHED_SEC) { t -= SCHED_SEC; s++; }
	while (t < 0) { t += SCHED_SEC; s--; }
	return (uint64_t)s * hz + s_mul_frac(t, hz);
}

// ---- the CPUs (device_execute_interface) -----------------------------------------------
typedef struct {
	sched_time_t local;                 // m_localtime
	int64_t aspc;                       // m_attoseconds_per_cycle
	int64_t aspclk;                     // the device's attoseconds a clock
	uint32_t clock, divider, cps;       // m_cycles_per_second
	int divshift;
	uint32_t divisor;
	uint32_t inv;                       // 2^62 / divisor, for s_div (0: divide)
	int64_t minq;                       // minimum_quantum()
	uint64_t total;                     // m_totalcycles
	uint32_t suspend, nextsuspend;
	bool eat, nexteat;                  // m_eatcycles, m_nexteatcycles
} s_exec_t;

static s_exec_t s_ex[SCHED_CPUS];
static uint8_t s_order[SCHED_CPUS];     // m_execute_list: the running ones, then the suspended, each in device order
static int s_executing = -1;
static bool s_pending;                  // m_suspend_changes_pending

// MAME's divu_64x32(x, d) without the library's 64-bit divide (the M33
// has none): x times 2^62 / d, one short at most, put right. For d over
// 2^30 (any CPU under 900 MHz) and x under 2^40; else the divide.
SCHED_INLINE int s_div(uint64_t x, const s_exec_t *e) {
	if (__builtin_expect((x >> 40) != 0 || e->inv == 0, 0)) return (int)(x / e->divisor);
	uint32_t xl = (uint32_t)x, xh = (uint32_t)(x >> 32);
	uint64_t t = (uint64_t)xh * e->inv + (((uint64_t)xl * e->inv) >> 32);
	uint32_t q = (uint32_t)(t >> 30);
	if (x - (uint64_t)q * e->divisor >= e->divisor) q++;
	return (int)q;
}

static void s_rebuild_execute_list(void) {
	int n = 0;
	for (int d = 0; d < SCHED_CPUS; d++) if (!s_ex[d].suspend) s_order[n++] = (uint8_t)d;
	for (int d = 0; d < SCHED_CPUS; d++) if (s_ex[d].suspend) s_order[n++] = (uint8_t)d;
}

static void s_apply_suspend_changes(void) {
	uint32_t changed = 0;
	for (int d = 0; d < SCHED_CPUS; d++) {
		s_exec_t *e = &s_ex[d];
		changed |= e->suspend ^ e->nextsuspend;
		e->suspend = e->nextsuspend;
		e->nextsuspend &= ~SCHED_SUSPEND_TIMESLICE;
		e->eat = e->nexteat;
	}
	if (changed) s_rebuild_execute_list();     // (the changes stay pending, as MAME's do, for one more look)
	else s_pending = false;
}

int sched_executing(void) { return s_executing; }

void sched_abort(void) {                // device_scheduler::abort_timeslice: the executing device's
	if (s_executing >= 0) SCHED_ABORT(s_executing);
}

static void s_changed(int d) {          // suspend_resume_changed
	s_pending = true;
	if (s_executing == d) SCHED_ABORT(d);
}

void sched_suspend(int d, uint32_t reason, bool eat) {
	s_ex[d].nextsuspend |= reason;
	s_ex[d].nexteat = eat;
	s_changed(d);
}

void sched_resume(int d, uint32_t reason) {
	s_ex[d].nextsuspend &= ~reason;
	s_changed(d);
}

bool sched_suspended(int d, uint32_t reason) { return (s_ex[d].nextsuspend & reason) != 0; }

void sched_yield(int d) { sched_suspend(d, SCHED_SUSPEND_TIMESLICE, false); }

// machine().time(): the timer's time in a callback, the running device's while it runs, else the base
static bool s_in_cb;
static sched_time_t s_cb_expire;
SCHED_INLINE sched_time_t s_now(void) {
	if (s_in_cb) return s_cb_expire;
	if (s_executing >= 0) {
		const s_exec_t *e = &s_ex[s_executing];
		return e->local + (sched_time_t)(SCHED_DONE(s_executing)) * e->divider * e->aspclk;
	}
	return s_base;
}
sched_time_t sched_now(void) { return s_now(); }
sched_time_t sched_base(void) { return s_base; }
uint32_t sched_epoch(void) { return s_epoch; }

sched_time_t sched_local_time(int d) {
	const s_exec_t *e = &s_ex[d];
	if (s_executing == d) return e->local + (sched_time_t)(SCHED_DONE(d)) * e->divider * e->aspclk;
	return e->local;
}

uint64_t sched_total_cycles(int d) {
	return s_ex[d].total + (s_executing == d ? (uint64_t)(SCHED_DONE(d)) : 0);
}

// ---- quanta --------------------------------------------------------------------------------
typedef struct { sched_time_t requested, actual, expire; } s_quantum_t;
static s_quantum_t s_q[SCHED_QUANTA];   // m_quantum_list: by requested, the smallest (the one used) first
static int s_nq;
static int64_t s_qmin;                  // m_quantum_minimum: the perfect interleave
static sched_time_t s_max_quantum;      // the config's maximum_quantum
static int s_perfect_cpu;               // the config's perfect_quantum_device, -1 none
static int s_ncpus;                     // CPUs set up so far

// compute_perfect_interleave: the second smallest of the CPUs' minimum quanta
static void s_perfect_interleave(void) {
	if (s_ncpus == 0) return;
	int64_t smallest = s_ex[0].minq, perfect = SCHED_SEC - 1;
	for (int d = 1; d < s_ncpus; d++) {
		int64_t q = s_ex[d].minq;
		if (q < smallest) { perfect = smallest; smallest = q; }
		else if (q < perfect) perfect = q;
	}
	s_qmin = perfect;
	for (int i = 0; i < s_nq; i++) s_q[i].actual = s_q[i].requested > s_qmin ? s_q[i].requested : s_qmin;
}

static void s_quantum_insert(sched_time_t quantum, sched_time_t duration);
static void s_base_quantum(void) {      // rebuild_execute_list's first: the core scheduling quantum
	sched_time_t q = SCHED_HZ(60);
	if (s_max_quantum < q) q = s_max_quantum;
	if (s_perfect_cpu >= 0 && s_ex[s_perfect_cpu].minq < q) q = s_ex[s_perfect_cpu].minq;
	s_quantum_insert(q, SCHED_NEVER);
}

static void s_quantum_insert(sched_time_t quantum, sched_time_t duration) {
	sched_time_t cur = s_now();
	sched_time_t expire = duration == SCHED_NEVER ? SCHED_NEVER : cur + duration;
	int ins = -1;
	for (int i = 0; i < s_nq;) {
		if (cur >= s_q[i].expire) {     // out of date: gone
			memmove(&s_q[i], &s_q[i + 1], (size_t)(s_nq - i - 1) * sizeof s_q[0]);
			s_nq--;
			continue;
		}
		if (s_q[i].requested <= quantum) ins = i;   // (after the last no longer than it)
		i++;
	}
	if (ins >= 0 && s_q[ins].requested == quantum) {
		if (expire > s_q[ins].expire) s_q[ins].expire = expire;
		return;
	}
	if (s_nq == SCHED_QUANTA) SCHED_OVERFLOW();
	memmove(&s_q[ins + 2], &s_q[ins + 1], (size_t)(s_nq - ins - 1) * sizeof s_q[0]);
	s_q[ins + 1].requested = quantum;
	s_q[ins + 1].actual = quantum > s_qmin ? quantum : s_qmin;
	s_q[ins + 1].expire = expire;
	s_nq++;
}

void sched_add_quantum(sched_time_t quantum, sched_time_t duration) {
	if (s_nq == 0) s_base_quantum();
	s_quantum_insert(quantum, duration);
}

void sched_perfect_quantum(sched_time_t duration) { sched_add_quantum(0, duration); }

// ---- timers: a list in expiry order, ties first come first -------------------------------
typedef struct {
	sched_time_t expire, period, start;
	sched_cb_t cb;
	int param;
	int16_t next;                       // the next in the list, -1 at its end
	bool enabled, listed, used, temporary;
} s_timer_t;

static s_timer_t s_tm[SCHED_TIMERS];
static int s_head = -1;
static int s_cb_timer = -1;
static bool s_cb_modified;

static void s_tm_remove(int i) {
	if (!s_tm[i].listed) return;
	if (s_head == i) s_head = s_tm[i].next;
	else {
		int p = s_head;
		while (s_tm[p].next != i) p = s_tm[p].next;
		s_tm[p].next = s_tm[i].next;
	}
	s_tm[i].listed = false;
}

// timer_list_insert: before the first that expires later (a disabled or never one isn't listed)
static void s_tm_insert(int i) {
	s_timer_t *t = &s_tm[i];
	if (!t->enabled || t->expire == SCHED_NEVER) return;
	int prev = -1, cur = s_head;
	while (cur >= 0 && s_tm[cur].expire <= t->expire) { prev = cur; cur = s_tm[cur].next; }
	t->next = (int16_t)cur;
	if (prev < 0) s_head = i;
	else s_tm[prev].next = (int16_t)i;
	t->listed = true;
}

SCHED_INLINE sched_time_t s_head_expire(void) { return s_head >= 0 ? s_tm[s_head].expire : SCHED_NEVER; }

static int s_tm_new(sched_cb_t cb, bool temporary) {
	for (int i = 0; i < SCHED_TIMERS; i++) {
		s_timer_t *t = &s_tm[i];
		if (t->used) continue;
		memset(t, 0, sizeof *t);
		t->used = true;
		t->temporary = temporary;
		t->cb = cb;
		t->period = SCHED_NEVER;
		t->expire = SCHED_NEVER;
		t->start = s_now();
		t->next = -1;
		return i;
	}
	SCHED_OVERFLOW();
	return -1;
}

int sched_timer_alloc(sched_cb_t cb) { return s_tm_new(cb, false); }

void sched_timer_adjust(int i, sched_time_t delay, int param, sched_time_t period) {
	s_timer_t *t = &s_tm[i];
	if (s_in_cb && s_cb_timer == i) s_cb_modified = true;
	t->param = param;
	t->enabled = true;
	if (delay < 0) delay = 0;
	t->start = s_now();
	t->expire = delay == SCHED_NEVER ? SCHED_NEVER : t->start + delay;
	t->period = period;
	s_tm_remove(i);
	s_tm_insert(i);
	if (s_head == i) sched_abort();
}

void sched_timer_adjust_at(int i, sched_time_t when, int param, sched_time_t period) {
	sched_timer_adjust(i, when == SCHED_NEVER ? SCHED_NEVER : when - s_now(), param, period);
}

bool sched_timer_enable(int i, bool on) {
	s_timer_t *t = &s_tm[i];
	bool old = t->enabled;
	if (old != on) {
		t->enabled = on;
		s_tm_remove(i);
		s_tm_insert(i);
	}
	return old;
}

bool sched_timer_enabled(int i) { return s_tm[i].enabled; }
int sched_timer_param(int i) { return s_tm[i].param; }
sched_time_t sched_timer_expire(int i) { return s_tm[i].expire; }
sched_time_t sched_timer_remaining(int i) {
	sched_time_t cur = s_now();
	if (s_tm[i].expire == SCHED_NEVER) return SCHED_NEVER;
	return cur >= s_tm[i].expire ? 0 : s_tm[i].expire - cur;
}
sched_time_t sched_timer_elapsed(int i) { return s_now() - s_tm[i].start; }

void sched_timer_set(sched_time_t delay, sched_cb_t cb, int param) {
	int i = s_tm_new(cb, true);
	s_timer_t *t = &s_tm[i];
	t->param = param;
	if (delay < 0) delay = 0;
	t->expire = delay == SCHED_NEVER ? SCHED_NEVER : t->start + delay;
	t->enabled = t->expire != SCHED_NEVER;
	s_tm_insert(i);
	if (s_head == i) sched_abort();
}

void sched_synchronize(sched_cb_t cb, int param) { sched_timer_set(0, cb, param); }

static SCHED_HOT void s_execute_timers(void) {
	while (s_head >= 0 && s_tm[s_head].expire <= s_base) {
		int h = s_head;
		s_timer_t *t = &s_tm[h];
		bool was_enabled = t->enabled;
		if (t->period == 0 || t->period == SCHED_NEVER) t->enabled = false;
		s_in_cb = true;
		s_cb_timer = h;
		s_cb_expire = t->expire;
		s_cb_modified = false;
		if (was_enabled && t->cb) t->cb(t->param);
		if (!s_cb_modified) {
			if (!t->temporary) {        // schedule_next_period
				t->start = t->expire;
				t->expire = t->period == SCHED_NEVER ? SCHED_NEVER : t->expire + t->period;
				s_tm_remove(h);
				s_tm_insert(h);
			} else {
				s_tm_remove(h);
				t->used = false;
			}
		}
		s_in_cb = false;
		s_cb_timer = -1;
	}
}

// ---- input lines (device_execute_interface::device_input) ------------------------------
typedef struct {
	int32_t queue[32];                  // state | vector << 8
	int n;                              // m_qindex
	int d, kind;
	bool used;
	int cur, curvector, stored, def;    // m_curstate, m_curvector, m_stored_vector, default_irq_vector
	void (*set)(int state);
} s_line_t;
static s_line_t s_ln[SCHED_LINES];

void sched_line(int line, int d, int kind, int vector, void (*set)(int state)) {
	s_line_t *l = &s_ln[line];
	memset(l, 0, sizeof *l);
	l->used = true;
	l->d = d;
	l->kind = kind;
	l->def = l->stored = l->curvector = vector;
	l->cur = SCHED_CLEAR_LINE;
	l->set = set;
}

void sched_reset_cpu(int d) {
	s_ex[d].total = 0;                  // interface_pre_reset
	sched_resume(d, SCHED_SUSPEND_ANY);
	for (int i = 0; i < SCHED_LINES; i++)      // interface_post_reset: the vectors
		if (s_ln[i].used && s_ln[i].d == d) s_ln[i].curvector = s_ln[i].stored = s_ln[i].def;
}

static void s_empty_event_queue(int line) {
	s_line_t *l = &s_ln[line];
	for (int k = 0; k < l->n; k++) {
		int32_t ev = l->queue[k];
		l->cur = ev & 0xff;
		l->curvector = ev >> 8;
		if (l->kind == SCHED_LINE_RESET) {
			if (l->cur == SCHED_ASSERT_LINE) sched_suspend(l->d, SCHED_SUSPEND_RESET, true);
			else if (sched_suspended(l->d, SCHED_SUSPEND_RESET)) {
				sched_reset_cpu(l->d);  // device().reset()
				SCHED_RESET(l->d);
				sched_resume(l->d, SCHED_SUSPEND_RESET);
			}
		} else if (l->kind == SCHED_LINE_HALT) {
			if (l->cur == SCHED_ASSERT_LINE) sched_suspend(l->d, SCHED_SUSPEND_HALT, true);
			else if (l->cur == SCHED_CLEAR_LINE) sched_resume(l->d, SCHED_SUSPEND_HALT);
		} else if (l->set) {
			l->set(l->cur != SCHED_CLEAR_LINE);
		}
	}
	l->n = 0;
}

void sched_set_input_and_vector(int line, int state, int vector) {
	s_line_t *l = &s_ln[line];
	if (l->n == (int)(sizeof l->queue / sizeof l->queue[0])) {    // full: applied now (MAME logs it)
		l->n--;
		s_empty_event_queue(line);
	}
	if (vector < 0) vector = l->stored;
	l->queue[l->n++] = (int32_t)((state & 0xff) | (vector << 8));
	if (l->n == 1) sched_synchronize(s_empty_event_queue, line);
}

void sched_set_input(int line, int state) { sched_set_input_and_vector(line, state, -1); }
void sched_set_input_vector(int line, int vector) { s_ln[line].stored = vector; }
int sched_input_state(int line) { return s_ln[line].cur; }

int sched_irq_ack(int line) {           // default_irq_callback
	s_line_t *l = &s_ln[line];
	int vector = l->curvector;
	if (l->cur == SCHED_HOLD_LINE) {
		if (l->set) l->set(0);
		l->cur = SCHED_CLEAR_LINE;
	}
	return vector;
}

// ---- times the board keeps -------------------------------------------------------------
static sched_time_t *s_keep[SCHED_KEEPS];
static int s_nkeep;
void sched_keep(sched_time_t *t) {
	for (int i = 0; i < s_nkeep; i++) if (s_keep[i] == t) return;
	if (s_nkeep == SCHED_KEEPS) SCHED_OVERFLOW();
	s_keep[s_nkeep++] = t;
}

static void s_rebase(void) {            // a second on: every time less 1e18
	s_epoch++;
	s_base -= SCHED_SEC;
	for (int d = 0; d < SCHED_CPUS; d++) s_ex[d].local -= SCHED_SEC;
	for (int i = 0; i < SCHED_TIMERS; i++) {
		if (!s_tm[i].used) continue;
		if (s_tm[i].expire != SCHED_NEVER) s_tm[i].expire -= SCHED_SEC;
		s_tm[i].start -= SCHED_SEC;
	}
	for (int i = 0; i < s_nq; i++) if (s_q[i].expire != SCHED_NEVER) s_q[i].expire -= SCHED_SEC;
	for (int i = 0; i < s_nkeep; i++) if (*s_keep[i] != SCHED_NEVER) *s_keep[i] -= SCHED_SEC;
}

// ---- the timeslice -------------------------------------------------------------------------
// one device's part of a slice (the loop in device_scheduler::timeslice):
// one suspended with its cycles eaten has them counted, not run
SCHED_INLINE void s_slice(int d, sched_time_t *target) {
	s_exec_t *e = &s_ex[d];
	if (e->suspend && !e->eat) return;
	int64_t delta = *target - e->local;
	if (delta < e->aspc) return;
	int ran = s_div((uint64_t)delta >> e->divshift, e);
	if (!e->suspend) {
		s_executing = d;
		ran = SCHED_RUN(d, ran);
		s_executing = -1;
	}
	e->total += (uint64_t)ran;
	if (__builtin_expect((uint32_t)ran < e->cps, 1)) e->local += (int64_t)ran * e->aspc;
	else e->local += (int64_t)((uint32_t)ran / e->cps) * SCHED_SEC + (int64_t)((uint32_t)ran % e->cps) * e->aspc;
	if (e->local < *target) *target = e->local > s_base ? e->local : s_base;
}

SCHED_HOT void sched_timeslice(void) {
	if (s_base >= SCHED_SEC) s_rebase();
	if (s_nq == 0) s_base_quantum();
	while (s_base >= s_q[0].expire) {   // an expired quantum goes
		memmove(&s_q[0], &s_q[1], (size_t)(s_nq - 1) * sizeof s_q[0]);
		s_nq--;
	}
	sched_time_t he;
	while (s_base < (he = s_head_expire())) {
		sched_time_t target = s_base + s_q[0].actual;
		if (he < target) target = he;
		if (s_pending) s_apply_suspend_changes();
		for (int i = 0; i < SCHED_CPUS; i++) s_slice(s_order[i], &target);
		s_base = target;
	}
	s_execute_timers();
}

void sched_run_to(uint64_t tick, uint32_t hz) {
	const uint64_t q = (uint64_t)SCHED_SEC / hz, r = (uint64_t)SCHED_SEC % hz;
	const uint64_t sec = tick / hz, rem = tick % hz;
	const sched_time_t thr = (sched_time_t)(rem * q + (rem * r + hz - 1) / hz);
	for (;;) {
		int64_t ds = (int64_t)sec - (int64_t)s_epoch;
		if (ds < 0 || (ds <= 8 && s_base >= ds * SCHED_SEC + thr)) break;
		sched_timeslice();
	}
}

// ---- power on ------------------------------------------------------------------------------
void sched_init(void) {
	s_epoch = 0;
	s_base = 0;
	memset(s_ex, 0, sizeof s_ex);
	for (int d = 0; d < SCHED_CPUS; d++) s_order[d] = (uint8_t)d;
	s_executing = -1;
	s_pending = false;
	s_in_cb = false;
	s_cb_timer = -1;
	memset(s_tm, 0, sizeof s_tm);
	s_head = -1;
	s_nq = 0;
	s_qmin = 0;
	s_max_quantum = SCHED_NEVER;
	s_perfect_cpu = -1;
	s_ncpus = 0;
	memset(s_ln, 0, sizeof s_ln);
	s_nkeep = 0;
}

void sched_cpu(int d, uint32_t clock, uint32_t divider, int min_cycles) {
	s_exec_t *e = &s_ex[d];
	memset(e, 0, sizeof *e);
	e->clock = clock;
	e->divider = divider ? divider : 1;
	e->cps = (uint32_t)(((uint64_t)clock + e->divider - 1) / e->divider);   // execute_clocks_to_cycles
	e->aspc = SCHED_SEC / e->cps;
	e->aspclk = SCHED_SEC / clock;
	int64_t attos = e->aspc;
	e->divshift = 0;
	while (attos >= (1LL << 31)) { e->divshift++; attos >>= 1; }
	e->divisor = (uint32_t)attos;
	e->inv = e->divisor > (1u << 30) ? (uint32_t)((1ull << 62) / e->divisor) : 0;
	e->minq = e->aspc * (min_cycles > 0 ? min_cycles : 1);
	e->suspend = SCHED_SUSPEND_RESET;   // interface_pre_start
	if (d + 1 > s_ncpus) s_ncpus = d + 1;
	s_rebuild_execute_list();
	s_perfect_interleave();
}

void sched_max_quantum(sched_time_t q) { s_max_quantum = q; }
void sched_perfect_cpu(int d) { s_perfect_cpu = d; }

#undef SCHED_HOT
#undef SCHED_INLINE

#endif  // SCHED_CPUS
