// upy-android: per-call output capture (crash-mitigation requirement #2).
//
// mp_obj_print_exception() (called by mp_embed_exec_str on an uncaught
// exception) goes through this same function, so tracebacks land in the
// captured buffer exactly like normal print() output would on a real
// terminal -- no separate "traceback string" field needed. The engine's
// JNI wrapper is expected to call mp_embed_output_clear() before, then
// mp_embed_output_get() after, each mp_embed_exec_str() call.

#include <string.h>
#include "py/mphal.h"
#include "micropython_embed.h"

#define MP_EMBED_OUTPUT_BUF_SIZE (32 * 1024)

static char mp_embed_output_buf[MP_EMBED_OUTPUT_BUF_SIZE];
static size_t mp_embed_output_len;

void mp_embed_output_clear(void) {
    mp_embed_output_len = 0;
}

const char *mp_embed_output_get(void) {
    mp_embed_output_buf[mp_embed_output_len] = '\0';
    return mp_embed_output_buf;
}

// Live output tap (see micropython_embed.h). Deliberately separate from
// the accumulate-into-mp_embed_output_buf logic above: the chunk callback
// always sees the full, untruncated write, even once the 32KB
// accumulation buffer has filled up (a long-running streamed script's
// live view isn't bounded by that cap, only the final post-return
// snapshot is).
static mp_embed_output_chunk_cb_t mp_embed_output_chunk_cb;
static void *mp_embed_output_chunk_cb_context;

void mp_embed_set_output_chunk_cb(mp_embed_output_chunk_cb_t cb, void *context) {
    mp_embed_output_chunk_cb = cb;
    mp_embed_output_chunk_cb_context = context;
}

// random module seed (see MICROPY_PY_RANDOM_SEED_INIT_FUNC in
// mpconfigport.h). arc4random_buf() is Bionic's standard entropy source
// (stdlib.h), available since long before our minSdk 26 -- no fd/
// permission handling needed, unlike ports/unix's getrandom()/
// /dev/random fallback.
#include <stdlib.h>

unsigned long mp_android_random_seed_init(void) {
    unsigned long seed;
    arc4random_buf(&seed, sizeof(seed));
    return seed;
}

// time module HAL primitives (see mpconfigport.h's MICROPY_PY_TIME block).
// clock_gettime(CLOCK_MONOTONIC) mirrors ports/unix's own mp_hal_ticks_ms/
// us implementation exactly -- standard POSIX, nothing Bionic-specific.
#include <time.h>

mp_uint_t mp_hal_ticks_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (mp_uint_t)ts.tv_sec * 1000 + (mp_uint_t)ts.tv_nsec / 1000000;
}

mp_uint_t mp_hal_ticks_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (mp_uint_t)ts.tv_sec * 1000000 + (mp_uint_t)ts.tv_nsec / 1000;
}

uint64_t mp_hal_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Chunked, NOT busy-waiting: each chunk is a real nanosleep() (thread
// actually blocked/parked by the OS scheduler, no CPU spent), with a
// mp_handle_pending() check between chunks so a long time.sleep() is
// genuinely interruptible via the app's existing Interrupt button
// (mp_sched_keyboard_interrupt() sets a pending exception that
// MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS raises via nlr_raise,
// unwinding out of this loop) -- not just at fixed VM bytecode points.
// 5ms chunk = worst-case interrupt latency, negligible per-chunk syscall
// overhead at this granularity.
#define MP_HAL_DELAY_CHUNK_MS (5)

void mp_hal_delay_ms(mp_uint_t ms) {
    while (ms > 0) {
        mp_uint_t chunk = ms < MP_HAL_DELAY_CHUNK_MS ? ms : MP_HAL_DELAY_CHUNK_MS;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = (long)chunk * 1000000L };
        nanosleep(&ts, NULL);
        ms -= chunk;
        mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    // Short enough (sleep_us is for microsecond-scale delays) that a
    // single nanosleep needs no chunking/interrupt-check in between.
    struct timespec ts = { .tv_sec = us / 1000000, .tv_nsec = (long)(us % 1000000) * 1000L };
    nanosleep(&ts, NULL);
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    size_t copy_len = len;
    size_t space = MP_EMBED_OUTPUT_BUF_SIZE - 1 - mp_embed_output_len;
    if (copy_len > space) {
        copy_len = space;
    }
    if (copy_len > 0) {
        memcpy(mp_embed_output_buf + mp_embed_output_len, str, copy_len);
        mp_embed_output_len += copy_len;
    }
    if (mp_embed_output_chunk_cb != NULL && len > 0) {
        mp_embed_output_chunk_cb(str, len, mp_embed_output_chunk_cb_context);
    }
}
