// upy-android native csi (camera) module. OUR OWN code, NOT vendored
// OpenMV source. Compiled directly by the app's own CMake target, not
// copied via apply-overrides.sh.
#ifndef UPY_ANDROID_CAMERA_MODULE_H
#define UPY_ANDROID_CAMERA_MODULE_H

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

#ifdef __cplusplus
}
#endif

#endif
