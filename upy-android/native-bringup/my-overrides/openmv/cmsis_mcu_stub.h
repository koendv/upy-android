// upy-android OpenMV support layer -- pointed to by board_config.h's
// CMSIS_MCU_H define. Real boards point this at their vendor MCU header
// (e.g. stm32h7xx.h) via `#include CMSIS_MCU_H`. Nothing we compile
// actually needs CMSIS SFR/intrinsic definitions once __ARM_ARCH is
// forced below 7/8 (see arm_math.h) -- this file only needs to
// exist so the #include resolves.
