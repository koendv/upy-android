// upy-android OpenMV support layer -- stub replacement for OpenMV's
// cmsis_extension.h.
//
// Real finding: lib/imlib/simd.h picks its vectorized-type path with
// `#if (__ARM_ARCH >= 8)`, intending to distinguish "generic portable
// fallback" (older Cortex-M, e.g. ARMv7E-M / __ARM_ARCH==7) from "real
// ARM SIMD available" (their newest Cortex-M55/M85 Helium/MVE boards,
// ARMv8.1-M / __ARM_ARCH==8) -- but clang targeting aarch64 Android
// (Cortex-A, ARMv8-A) ALSO defines __ARM_ARCH as 8, and takes the same
// branch simd.h uses for Helium/MVE, which references mve_pred16_t and
// other M-profile/Helium-only intrinsic types that don't exist for an
// A-profile/aarch64 target at all -- a genuine version-number collision
// between two unrelated ARM profiles, not something fixable by providing
// more headers.
//
// The actual __ARM_ARCH override lives in arm_math.h, not here --
// it needs to take effect before fmath.h's OWN __ARM_ARCH check too
// (inline VFP assembly, a separate finding -- see arm_math.h), and
// arm_math.h is the common header both simd.h and fmath.h include first,
// while cmsis_extension.h is only ever reached via simd.h. This file is
// otherwise a no-op -- kept so the #include <cmsis_extension.h> in
// simd.h/imlib.h still resolves.
#ifndef UPY_ANDROID_STUB_CMSIS_EXTENSION_H
#define UPY_ANDROID_STUB_CMSIS_EXTENSION_H

#endif
