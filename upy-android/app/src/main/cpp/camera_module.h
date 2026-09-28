// upy-android native csi (camera) module.
// micropython android port only.
// Compiled directly by the app's own CMake target, not copied via apply-overrides.sh.
#ifndef UPY_ANDROID_CAMERA_MODULE_H
#define UPY_ANDROID_CAMERA_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void camera_close_all(void);

// Safe to call from any thread, same contract as nativeInterrupt() itself (engine_jni.cpp).
// Posts into the currently in-flight csi.snapshot() call's wait semaphore if one exists, a no-op otherwise.
// This is what makes an in-progress snapshot() actually interruptible rather than only timing out after 500ms.
void camera_interrupt_active_wait(void);

// Reports the csi module's currently configured capture resolution and format
void camera_get_current_size(int32_t *width, int32_t *height, bool *color);

#ifdef __cplusplus
}
#endif

#endif
