// Define so there's no dependency on extmod/virtpin.h
#define mp_hal_pin_obj_t

// time.ticks_cpu(): no cheap, reliable userspace CPU-cycle-counter read on
// ARM64 Android (would need kernel-permitted cntvct_el0 access, fragile
// across devices/kernels for something scripts only use for relative
// profiling, never correctness). ports/unix itself just hardcodes 0 for
// the same reason on general-purpose OSes -- matching that precedent
// rather than over-engineering.
#define mp_hal_ticks_cpu() 0

// MICROPY_KBD_EXCEPTION needs mp_hal_set_interrupt_char(), provided by the
// shared implementation (vendored into micropython_embed/shared/runtime/
// since embed.mk's copy list doesn't include it by default).
#include "shared/runtime/interrupt_char.h"

// extmod/vfs_posix.c and vfs_posix_file.c expect the port to provide this
// (used by ports/unix/mphalport.h originally) -- retries a syscall on
// EINTR per PEP 475. MP_THREAD_GIL_ENTER/EXIT are no-ops without
// MICROPY_PY_THREAD (our case), so this reduces to a plain EINTR-retry
// loop.
#include <errno.h>
#include "py/mpthread.h"
#include "py/runtime.h"

#define MP_HAL_RETRY_SYSCALL(ret, syscall, raise) { \
        for (;;) { \
            MP_THREAD_GIL_EXIT(); \
            ret = syscall; \
            MP_THREAD_GIL_ENTER(); \
            if (ret == -1) { \
                int err = errno; \
                if (err == EINTR) { \
                    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS); \
                    continue; \
                } \
                raise; \
            } \
            break; \
        } \
}
