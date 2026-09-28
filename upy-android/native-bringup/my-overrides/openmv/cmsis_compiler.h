// upy-android OpenMV support layer. Stub for ARM CMSIS-Core's
// cmsis_compiler.h. common/memcpy.c #includes it. Its own __ARM_ARCH-
// gated fast paths are already routed to plain portable fallbacks by
// the forced __ARM_ARCH=6 (see arm_math.h), except one real exception:
// unaligned_memcpy_rev16() calls __REV16() unconditionally, not gated
// by __ARM_ARCH, so that one intrinsic is genuinely needed here. Same
// portable byte-swap-each-halfword implementation as arm_math.h's
// copy, duplicated rather than shared since no single translation
// unit includes both headers, so there's no redefinition risk.
#ifndef UPY_ANDROID_STUB_CMSIS_COMPILER_H
#define UPY_ANDROID_STUB_CMSIS_COMPILER_H

#include <stdint.h>

static inline uint32_t __REV16(uint32_t val) {
    uint32_t lo = val & 0xFFFF, hi = (val >> 16) & 0xFFFF;
    lo = (uint32_t) ((lo >> 8) | (lo << 8)) & 0xFFFF;
    hi = (uint32_t) ((hi >> 8) | (hi << 8)) & 0xFFFF;
    return (hi << 16) | lo;
}

#endif
