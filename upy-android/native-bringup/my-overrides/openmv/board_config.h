// upy-android OpenMV support layer -- OUR OWN board_config.h, written
// from scratch. Real OpenMV boards' board_config.h files are ~100-300
// lines of MCU-pin/peripheral config that has no meaning on Android;
// this starts empty and grows only when a real compiler error demands a
// specific symbol, so it stays an honest record of what imlib/py_image.c
// actually need from a "board" versus what's MCU-specific noise.
#ifndef UPY_ANDROID_BOARD_CONFIG_H
#define UPY_ANDROID_BOARD_CONFIG_H

// CMSIS_MCU_H (real boards point this at their vendor MCU header, e.g.
// "stm32h7xx.h" -- see cmsis_mcu_stub.h) is NOT defined here: imlib.h
// includes fmath.h (which needs it) BEFORE it includes board_config.h,
// so a #define here would always be too late. Must be supplied via
// -DCMSIS_MCU_H='"cmsis_mcu_stub.h"' on the compile command line instead
// -- which also matches how a real board's own Makefile supplies it.

#endif
