// upy-android native csi (camera) module. OUR OWN code, NOT vendored
// OpenMV source. Compiled directly by the app's own CMake target, not
// copied via apply-overrides.sh.
#ifndef UPY_ANDROID_CAMERA_MODULE_H
#define UPY_ANDROID_CAMERA_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Idempotent, safe to call when nothing is open. Most scripts never
// touch the camera, so this runs on every ordinary Reset too. Called
// from BOTH Java_..._Engine_nativeReset() (the only teardown path where
// the :engine process itself does not die, so nothing else closes an
// open camera device) and Java_..._Engine_nativeDeinit() (defense in
// depth).
void camera_close_all(void);

// Safe to call from any thread, same contract as nativeInterrupt()
// itself (engine_jni.cpp). Posts into the currently in-flight
// csi.snapshot() call's wait semaphore if one exists, a no-op
// otherwise. This is what makes an in-progress snapshot() actually
// interruptible rather than only timing out after 500ms.
void camera_interrupt_active_wait(void);

// Reports the csi module's currently configured capture resolution/
// format (g_cam.width/height/pixfmt -- set via csi.framesize()/
// csi.pixformat(), always a sane default even before reset(), see
// kDefaultWidth/kDefaultHeight). *color is true for PIXFORMAT_RGB565,
// false otherwise -- matching gif_add_frame()'s own two supported
// formats. Used by my-overrides/openmv/py_gif.c as its default source
// instead of OpenMV's own framebuffer_get(FB_MAINFB_ID) (a hardware
// sensor-framebuffer singleton this port never vendors) -- lets
// gif.Gif(path, loop=True) work with no explicit width=/height=/
// color=, matching the real unmodified upstream example script.
void camera_get_current_size(int32_t *width, int32_t *height, bool *color);

#ifdef __cplusplus
}
#endif

#endif
