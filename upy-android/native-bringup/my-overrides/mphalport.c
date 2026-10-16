// upy-android: per-call output capture (crash-mitigation requirement #2).
//
// mp_obj_print_exception() (called by mp_embed_exec_str on an uncaught
// exception) goes through this same function, so tracebacks land in the
// captured buffer exactly like normal print() output would on a real
// terminal. No separate "traceback string" field needed. The engine's
// JNI wrapper is expected to call mp_embed_output_clear() before, then
// mp_embed_output_get() after, each mp_embed_exec_str() call.

#include <pthread.h>
#include <string.h>
#include "py/mphal.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "shared/readline/readline.h"
#include "micropython_embed.h"

// Each run's output goes back over Binder twice (engine -> app -> adb), as
// UTF-16: 128 KB of text is ~256 KB per transaction, well below Binder's
// ~1 MB per-process limit.
#define MP_EMBED_OUTPUT_BUF_SIZE (128 * 1024)

static char mp_embed_output_buf[MP_EMBED_OUTPUT_BUF_SIZE];
static size_t mp_embed_output_len;

void mp_embed_output_clear(void) {
    mp_embed_output_len = 0;
}

const char *mp_embed_output_get(void) {
    // Buffer full: output was (probably) cut. Overwrite the tail with
    // "[truncated]" preceded by 1-4 dots: one dot, plus one for each
    // byte backed up to reach a UTF-8 lead byte, so no character is
    // split.
    static const char marker[] = "[truncated]";
    const size_t marker_len = sizeof(marker) - 1;
    if (mp_embed_output_len == MP_EMBED_OUTPUT_BUF_SIZE - 1) {
        size_t pos = mp_embed_output_len - marker_len - 1;
        while (pos > 0 && (mp_embed_output_buf[pos] & 0xC0) == 0x80) {
            pos--;
        }
        memset(mp_embed_output_buf + pos, '.', mp_embed_output_len - pos);
        memcpy(mp_embed_output_buf + mp_embed_output_len - marker_len, marker, marker_len);
    }
    mp_embed_output_buf[mp_embed_output_len] = '\0';
    return mp_embed_output_buf;
}

size_t mp_embed_output_get_len(void) {
    return mp_embed_output_len;
}

// Live output tap (see micropython_embed.h). Deliberately separate from
// the accumulate-into-mp_embed_output_buf logic above: the chunk callback
// always sees the full, untruncated write, even once the 128KB
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
// (stdlib.h), available on every Android version this app supports. No
// fd or permission handling needed, unlike ports/unix's getrandom()/
// /dev/random fallback.
#include <stdlib.h>

unsigned long mp_android_random_seed_init(void) {
    unsigned long seed;
    arc4random_buf(&seed, sizeof(seed));
    return seed;
}

// time module HAL primitives (see mpconfigport.h's MICROPY_PY_TIME
// block). clock_gettime(CLOCK_MONOTONIC) mirrors ports/unix's own
// mp_hal_ticks_ms/us implementation exactly, standard POSIX, nothing
// Bionic-specific.
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

// One doorbell for every wait of the interpreter thread. The pending
// exception (mp_sched_keyboard_interrupt) is the state; the doorbell only
// wakes the waiter, so a stale ring costs one extra loop pass.
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

static int doorbell_fd = -1;

__attribute__((constructor)) static void doorbell_create(void) {
    doorbell_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

// Any thread.
void mp_android_doorbell_ring(void) {
    uint64_t one = 1;
    ssize_t r = write(doorbell_fd, &one, sizeof(one));
    (void)r;
}

// timeout_ms < 0: no timeout.
void mp_android_doorbell_wait(long long timeout_ms) {
    // eventfd failed: wait in 20 ms slices, the caller's loop still sees Interrupt.
    if (doorbell_fd < 0 && (timeout_ms < 0 || timeout_ms > 20)) {
        timeout_ms = 20;
    }
    struct pollfd pfd = { .fd = doorbell_fd, .events = POLLIN };
    struct timespec ts = { .tv_sec = (time_t)(timeout_ms / 1000), .tv_nsec = (long)(timeout_ms % 1000) * 1000000L };
    if (ppoll(&pfd, 1, timeout_ms < 0 ? NULL : &ts, NULL) > 0) {
        uint64_t count;
        ssize_t r = read(doorbell_fd, &count, sizeof(count));
        (void)r;
    }
}

void mp_hal_delay_ms(mp_uint_t ms) {
    mp_uint_t start = mp_hal_ticks_ms();
    mp_uint_t elapsed;
    while ((elapsed = mp_hal_ticks_ms() - start) < ms) {
        mp_event_wait_ms(ms - elapsed);
    }
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
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

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    mp_hal_stdout_tx_strn_cooked(str, len);
    return len;
}

// stdin: a FIFO that the UI and adb fill (any thread) and the interpreter
// thread empties. The doorbell wakes a waiting read or poll.
#define STDIN_FIFO_SIZE (64 * 1024)

static pthread_mutex_t stdin_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t stdin_buf[STDIN_FIFO_SIZE];
static size_t stdin_head;
static size_t stdin_len;
static bool script_active;
static bool stdin_waiting;
static void (*stdin_waiting_cb)(int waiting);

void mp_android_stdin_set_waiting_cb(void (*cb)(int waiting)) {
    stdin_waiting_cb = cb;
}

static void stdin_set_waiting(bool waiting) {
    if (stdin_waiting != waiting) {
        stdin_waiting = waiting;
        if (stdin_waiting_cb != NULL) {
            stdin_waiting_cb(waiting);
        }
    }
}

// A script starts or ends: the FIFO is emptied, and a read that was
// interrupted no longer counts as waiting.
void mp_android_script_active(int active) {
    pthread_mutex_lock(&stdin_lock);
    script_active = active;
    stdin_len = 0;
    pthread_mutex_unlock(&stdin_lock);
    if (!active) {
        stdin_set_waiting(false);
    }
}

void mp_android_stdin_clear(void) {
    pthread_mutex_lock(&stdin_lock);
    stdin_len = 0;
    pthread_mutex_unlock(&stdin_lock);
}

// Returns the number of bytes accepted, or -1 when no script runs.
int mp_android_stdin_push(const void *data, size_t len) {
    pthread_mutex_lock(&stdin_lock);
    if (!script_active) {
        pthread_mutex_unlock(&stdin_lock);
        return -1;
    }
    size_t n = STDIN_FIFO_SIZE - stdin_len;
    if (n > len) {
        n = len;
    }
    for (size_t i = 0; i < n; i++) {
        stdin_buf[(stdin_head + stdin_len + i) % STDIN_FIFO_SIZE] = ((const uint8_t *)data)[i];
    }
    stdin_len += n;
    pthread_mutex_unlock(&stdin_lock);
    mp_android_doorbell_ring();
    return (int)n;
}

size_t mp_android_stdin_read_available(uint8_t *buf, size_t max) {
    pthread_mutex_lock(&stdin_lock);
    size_t n = stdin_len < max ? stdin_len : max;
    for (size_t i = 0; i < n; i++) {
        buf[i] = stdin_buf[(stdin_head + i) % STDIN_FIFO_SIZE];
    }
    stdin_head = (stdin_head + n) % STDIN_FIFO_SIZE;
    stdin_len -= n;
    pthread_mutex_unlock(&stdin_lock);
    return n;
}

// Blocks while the FIFO is empty. The UI is told once per blocking read.
int mp_hal_stdin_rx_chr(void) {
    uint8_t c;
    bool announced = false;
    for (;;) {
        if (mp_android_stdin_read_available(&c, 1) == 1) {
            if (announced) {
                stdin_set_waiting(false);
            }
            return c;
        }
        if (!announced) {
            announced = true;
            stdin_set_waiting(true);
        }
        mp_event_wait_indefinite();
    }
}

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = poll_flags & MP_STREAM_POLL_WR;
    pthread_mutex_lock(&stdin_lock);
    if ((poll_flags & MP_STREAM_POLL_RD) && stdin_len > 0) {
        ret |= MP_STREAM_POLL_RD;
    }
    pthread_mutex_unlock(&stdin_lock);
    return ret;
}

// input(): one line, without the editing and echo of readline(). The
// prompt was printed by input() already. The line ends at "\n"; "\r" is dropped.
int mp_android_readline(vstr_t *line, const char *prompt) {
    (void)prompt;
    for (;;) {
        int c = mp_hal_stdin_rx_chr();
        if (c == '\n') {
            return c;
        }
        if (c != '\r') {
            vstr_add_byte(line, c);
        }
    }
}
