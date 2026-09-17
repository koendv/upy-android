// upy-android OpenMV support layer -- Android replacement for
// OpenMV's common/umalloc.c (a TLSF-based pooled allocator with
// STM32-specific memory-region tagging -- ITCM/DTCM/DMA_D1/D2/D3 -- see
// SESSION_STATE.yaml). Android has a flat memory model, no such regions,
// so the memory-attribute bits in `flags` (UMA_ITCM etc, common/umalloc.h)
// are meaningless here and simply ignored, same tier of replacement as
// this project's own modtime_android.c.
//
// Confirmed via grep (SESSION_STATE.yaml) that only uma_malloc/calloc/
// realloc/free/avail are actually called anywhere in this port's vendored
// file set -- the pool-management/stats functions (uma_pool_*, uma_*_stats,
// uma_collect*) are declared in umalloc.h but never referenced, so they're
// deliberately NOT implemented here; if a later addition needs one, the
// linker will say so precisely.
#include <stdlib.h>
#include "umalloc.h"

void *uma_malloc(size_t size, uint32_t flags) {
    (void) flags;
    return malloc(size);
}

void *uma_calloc(size_t size, uint32_t flags) {
    (void) flags;
    return calloc(1, size);
}

void *uma_realloc(void *ptr, size_t size, uint32_t flags) {
    (void) flags;
    return realloc(ptr, size);
}

void uma_free(void *ptr) {
    free(ptr);
}

size_t uma_avail(uint32_t flags) {
    (void) flags;
    // No real pool accounting on Android's flat heap -- a large-but-finite
    // placeholder so callers that just sanity-check "is there roughly
    // enough room" don't misbehave. Revisit if a real caller needs an
    // honest number.
    return (size_t) 256 * 1024 * 1024;
}
