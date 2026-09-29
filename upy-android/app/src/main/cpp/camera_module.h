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
// Posts into the camera module's persistent frame-ready semaphore, a no-op if the camera has
// never been reset() at all. The semaphore is registered once, permanently (not per-call) --
// see camera_module.cpp's own ensure_session()/wait_for_frame() -- so this unblocks whichever
// interruptible wait, if any, is currently in progress (waiting for the first frame after
// reset()/a size change, or a csi_snapshot_warmup() call), rather than one specific snapshot()'s
// own short-lived wait the way it used to.
void camera_interrupt_active_wait(void);

// Reports the csi module's currently configured capture resolution and format
void camera_get_current_size(int32_t *width, int32_t *height, bool *color);

#ifdef __cplusplus
}
#endif

#endif
