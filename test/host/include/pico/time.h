// The Mac's stand-in for the pico-sdk's pico/time.h (and the parts of
// hardware/timer.h it brings), for test/host (see host.c) and the
// simulator: the models' clock, and alarms and repeating timers on it.
//
// The clock moves on 100 us with each time_us_32() or time_us_64() call,
// and by the time a sleep or the card's work takes. A timer's callback runs
// when its time has come and the plugin next calls the SDK's task or
// console functions (cvk_task(), cvf_task(), cvusb_task(), cvk_report(),
// cvf_frames() ...) or sleeps, on the plugin's own thread. On the cart it
// is an interrupt and can come between any two instructions, so the models
// show a timer's timing, not its races with the loop.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t absolute_time_t;           // microseconds since the start

uint32_t time_us_32(void);
uint64_t time_us_64(void);
void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);
void busy_wait_us(uint64_t us);
static inline void busy_wait_us_32(uint32_t us) { busy_wait_us(us); }
static inline void busy_wait_ms(uint32_t ms) { busy_wait_us((uint64_t)ms * 1000u); }

static inline absolute_time_t get_absolute_time(void) { return time_us_64(); }
static inline uint64_t to_us_since_boot(absolute_time_t t) { return t; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
static inline absolute_time_t delayed_by_us(absolute_time_t t, uint64_t us) { return t + us; }
static inline absolute_time_t delayed_by_ms(absolute_time_t t, uint32_t ms) { return t + (uint64_t)ms * 1000u; }
static inline absolute_time_t make_timeout_time_us(uint64_t us) { return time_us_64() + us; }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms) { return time_us_64() + (uint64_t)ms * 1000u; }
static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) { return (int64_t)(to - from); }
static inline bool time_reached(absolute_time_t t) { return time_us_64() >= t; }

// Alarms: the callback returns 0 to stop, a positive number of us to run
// again that long after it returns, or a negative one to run again that
// long after the time it was due. 16 at once, as the pico-sdk's default
// pool has; an id > 0, or -1 when all 16 are in use.
typedef int32_t alarm_id_t;
typedef int64_t (*alarm_callback_t)(alarm_id_t id, void *user_data);
alarm_id_t add_alarm_at(absolute_time_t time, alarm_callback_t callback, void *user_data, bool fire_if_past);
alarm_id_t add_alarm_in_us(uint64_t us, alarm_callback_t callback, void *user_data, bool fire_if_past);
alarm_id_t add_alarm_in_ms(uint32_t ms, alarm_callback_t callback, void *user_data, bool fire_if_past);
bool cancel_alarm(alarm_id_t alarm_id);

// Repeating timers: delay_us > 0 is the time from one callback's return to
// the next one's start, < 0 the time from one start to the next. The
// callback returns false to stop.
typedef struct repeating_timer repeating_timer_t;
typedef bool (*repeating_timer_callback_t)(repeating_timer_t *rt);
struct repeating_timer {
    int64_t delay_us;
    void *pool;
    alarm_id_t alarm_id;
    repeating_timer_callback_t callback;
    void *user_data;
};
bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t callback, void *user_data, repeating_timer_t *out);
static inline bool add_repeating_timer_ms(int32_t delay_ms, repeating_timer_callback_t callback, void *user_data,
                                          repeating_timer_t *out) {
    return add_repeating_timer_us((int64_t)delay_ms * 1000, callback, user_data, out);
}
bool cancel_repeating_timer(repeating_timer_t *timer);

#ifdef __cplusplus
}
#endif
