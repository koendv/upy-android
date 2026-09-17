// upy-android OpenMV support layer -- stub replacement for OpenMV's
// common/omv_cycles.h (real one needs CMSIS_MCU_H, a device-specific SDK
// header we don't have). Only real caller found in our copied set is
// imlib.h's imlib_poll_events()/imlib_poll_events_noexc() macros, which
// periodically yield to MicroPython's event loop during long-running
// algorithms so Interrupt stays responsive -- same purpose as this
// project's own mp_hal_delay_ms() chunking (see mphalport.c).
//
// "Cycles" here are just milliseconds (OMV_CPU_FREQ_HZ=1000, i.e. 1
// cycle == 1ms) via CLOCK_MONOTONIC, not real CPU cycles -- imlib.h's
// polling interval (20 "cycles") only cares about elapsed wall time, not
// an actual instruction-cycle count.
#ifndef UPY_ANDROID_STUB_OMV_CYCLES_H
#define UPY_ANDROID_STUB_OMV_CYCLES_H

#include <stdint.h>
#include <time.h>

#define OMV_CPU_FREQ_HZ      1000UL
#define OMV_CYCLES_PER_US    (OMV_CPU_FREQ_HZ / 1000000UL) // 0, unused (sub-ms not tracked)
#define OMV_CYCLES_PER_MS    (OMV_CPU_FREQ_HZ / 1000UL)    // 1

static inline void omv_cycles_init(void) {
}

static inline uint32_t omv_cycles_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) ((uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL);
}

#endif
