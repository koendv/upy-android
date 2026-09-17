// upy-android OpenMV support layer -- stub replacement for ARM's
// CMSIS-DSP arm_math.h. Confirmed via grep (see SESSION_STATE.yaml) that
// nothing in imlib/py_image.c calls a real arm_math.h function outside
// `#if defined(ARM_MATH_DSP)` guards, which we never define -- but
// lib/imlib/imlib.h #includes <arm_math.h> UNCONDITIONALLY, and
// lib/imlib/simd.h's portable (non-NEON) fallback path unconditionally
// uses the float32_t typedef, so the header must still exist and provide
// that one type.
//
// Also carries the __ARM_ARCH override both simd.h and fmath.h need (see
// cmsis_extension.h for the simd.h/Helium-MVE finding) -- this file
// is included first, by both of them, before either does its own
// __ARM_ARCH check, so it's the one place to do this once. Second,
// separate finding beyond the simd.h one: fmath.h ALSO branches on
// __ARM_ARCH (`#if (__ARM_ARCH >= 7)`) to pick inline 32-bit ARM/Thumb
// VFP assembly (`vsqrt.f32`, register constraint "=t") over a plain libm
// sqrtf() fallback -- that inline asm is invalid on aarch64 (wrong
// instruction set entirely, not just wrong calling convention), so
// __ARM_ARCH must land BELOW 7 here, not just below simd.h's threshold
// of 8 -- 6 clears both checks in one override.
#ifndef UPY_ANDROID_STUB_ARM_MATH_H
#define UPY_ANDROID_STUB_ARM_MATH_H

#include <stdint.h>

#ifdef __ARM_ARCH
#undef __ARM_ARCH
#endif
#define __ARM_ARCH 6

typedef float float32_t;

// Third finding, different from the __ARM_ARCH branches above: imlib.c
// calls a handful of real CMSIS-Core saturating-arithmetic intrinsics
// (__USAT/__SSAT/__PKHBT/__SMLAD) UNCONDITIONALLY -- not gated behind
// ARM_MATH_DSP or __ARM_ARCH at all, so OpenMV apparently treats these as
// "always available" on every board they ship, unlike the heavier
// arm_math.h DSP functions. Real CMSIS normally supplies these via
// core_cm*.h (pulled in transitively through CMSIS_MCU_H, which we stub
// to empty -- see board_config.h). Portable C equivalents below, per
// ARM's own public CMSIS-Core documentation of each intrinsic's
// semantics (saturate/pack/dual-multiply-accumulate) -- not copied from
// any specific vendor header, just the well-known bit-manipulation
// formulas any implementation of these semantics would use.
static inline int32_t __USAT(int32_t val, uint32_t sat) {
    int32_t max = (int32_t) ((1UL << sat) - 1);
    if (val > max) {
        return max;
    }
    if (val < 0) {
        return 0;
    }
    return val;
}

static inline int32_t __SSAT(int32_t val, uint32_t sat) {
    int32_t max = (int32_t) ((1UL << (sat - 1)) - 1);
    int32_t min = -(int32_t) (1UL << (sat - 1));
    if (val > max) {
        return max;
    }
    if (val < min) {
        return min;
    }
    return val;
}

#define __PKHBT(ARG1, ARG2, ARG3) \
    ((((int32_t) (ARG1)) & (int32_t) 0x0000FFFF) | (((int32_t) (ARG2) << (ARG3)) & (int32_t) 0xFFFF0000))

static inline int32_t __SMLAD(int32_t x, int32_t y, int32_t sum) {
    int16_t x_lo = (int16_t) (x & 0xFFFF);
    int16_t x_hi = (int16_t) (x >> 16);
    int16_t y_lo = (int16_t) (y & 0xFFFF);
    int16_t y_hi = (int16_t) (y >> 16);
    return sum + ((int32_t) x_lo * y_lo) + ((int32_t) x_hi * y_hi);
}

// __SMUAD: same dual 16x16 multiply as __SMLAD, no running accumulator.
static inline int32_t __SMUAD(int32_t x, int32_t y) {
    return __SMLAD(x, y, 0);
}

// __USAT16: saturates each 16-bit half of a packed 32-bit value
// independently (imlib/simd.h's vusat_s16_narrow_u8_lo macro applies it
// to a whole packed word at once, then masks/shifts the two halves back
// together itself -- this just needs to behave the same as two ordinary
// __USAT(., sat) calls on each 16-bit lane).
static inline uint32_t __USAT16(int32_t val, uint32_t sat) {
    int16_t lo = (int16_t) (val & 0xFFFF);
    int16_t hi = (int16_t) (val >> 16);
    uint32_t lo_sat = (uint32_t) __USAT(lo, sat) & 0xFFFF;
    uint32_t hi_sat = (uint32_t) __USAT(hi, sat) & 0xFFFF;
    return (hi_sat << 16) | lo_sat;
}

// __USAT_ASR(val, sat, shift): arithmetic-shift-right then unsigned-
// saturate -- filter.c's own call sites (e.g. __USAT_ASR(tmp, 8, 16))
// make the "shift then saturate" order unambiguous from usage.
static inline int32_t __USAT_ASR(int32_t val, uint32_t sat, uint32_t shift) {
    return __USAT(val >> shift, sat);
}

// __CLZ: count leading zeros of a 32-bit value (0 is defined as 32,
// matching the real CMSIS/ARM CLZ instruction's behavior, unlike plain
// __builtin_clz which is undefined for a zero input).
static inline uint32_t __CLZ(uint32_t val) {
    if (val == 0) {
        return 32;
    }
    return (uint32_t) __builtin_clz(val);
}

// __RBIT: reverse the bit order of a 32-bit value.
static inline uint32_t __RBIT(uint32_t val) {
    uint32_t result = 0;
    for (int i = 0; i < 32; i++) {
        result = (result << 1) | (val & 1);
        val >>= 1;
    }
    return result;
}

// __QADD16: adds each packed 16-bit half of x and y independently,
// saturating each half to the signed 16-bit range on overflow.
static inline uint32_t __QADD16(int32_t x, int32_t y) {
    int32_t x_lo = (int16_t) (x & 0xFFFF), x_hi = (int16_t) (x >> 16);
    int32_t y_lo = (int16_t) (y & 0xFFFF), y_hi = (int16_t) (y >> 16);
    int32_t sum_lo = x_lo + y_lo, sum_hi = x_hi + y_hi;
    if (sum_lo > 32767) { sum_lo = 32767; } else if (sum_lo < -32768) { sum_lo = -32768; }
    if (sum_hi > 32767) { sum_hi = 32767; } else if (sum_hi < -32768) { sum_hi = -32768; }
    return ((uint32_t) (sum_hi & 0xFFFF) << 16) | (uint32_t) (sum_lo & 0xFFFF);
}

// __SSUB16: subtracts each packed 16-bit half of y from x independently,
// wrapping (not saturating) on overflow -- same lane-split pattern as
// __QADD16 above, just subtract instead of saturating-add.
static inline uint32_t __SSUB16(int32_t x, int32_t y) {
    int16_t x_lo = (int16_t) (x & 0xFFFF), x_hi = (int16_t) (x >> 16);
    int16_t y_lo = (int16_t) (y & 0xFFFF), y_hi = (int16_t) (y >> 16);
    uint32_t diff_lo = (uint32_t) (int16_t) (x_lo - y_lo) & 0xFFFF;
    uint32_t diff_hi = (uint32_t) (int16_t) (x_hi - y_hi) & 0xFFFF;
    return (diff_hi << 16) | diff_lo;
}

// __REV16: byte-swaps each 16-bit halfword of a 32-bit value
// independently (endian-swap each half, not the whole word).
static inline uint32_t __REV16(uint32_t val) {
    uint32_t lo = val & 0xFFFF, hi = (val >> 16) & 0xFFFF;
    lo = (uint32_t) ((lo >> 8) | (lo << 8)) & 0xFFFF;
    hi = (uint32_t) ((hi >> 8) | (hi << 8)) & 0xFFFF;
    return (hi << 16) | lo;
}

#endif
