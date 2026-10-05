// upy-android camera module (CameraX).
// micropython android port only.
#ifndef UPY_ANDROID_CAMERA_MODULE_H
#define UPY_ANDROID_CAMERA_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called once from engine_jni.cpp's nativeInit(). jni_env is a JNIEnv*.
void camera_bridge_init(void *jni_env);

// Closes the open camera, if any. Before mp_embed_deinit().
void camera_close_all(void);

// Wakes a waiting snapshot() so Interrupt is seen at once. Any
// JVM-attached thread, like nativeInterrupt().
void camera_interrupt_active_wait(void);

// Size and format of the images snapshot() returns; 320x240 grayscale
// when no camera is open. Default for gif/mjpeg.
void camera_get_current_size(int32_t *width, int32_t *height, bool *color);

#ifdef __cplusplus
}
#endif

#endif
