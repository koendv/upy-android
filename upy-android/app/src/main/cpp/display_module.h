// upy-android native display module. OUR OWN code, NOT vendored OpenMV
// source, same tier as camera_module.h.
#ifndef UPY_ANDROID_DISPLAY_MODULE_H
#define UPY_ANDROID_DISPLAY_MODULE_H

struct ANativeWindow;

#ifdef __cplusplus
extern "C" {
#endif

// Takes ownership of new_window (already converted from a jobject via
// ANativeWindow_fromSurface() by the caller). This function is
// deliberately JNIEnv-free; engine_jni.cpp owns the JNI-specific half.
// Releases whatever window was previously set, under this module's own
// lock. Pass nullptr to detach: a script's display.SPIDisplay.write()
// calls silently no-op afterward rather than touching a stale handle.
void display_set_window(struct ANativeWindow *new_window);

#ifdef __cplusplus
}
#endif

#endif
