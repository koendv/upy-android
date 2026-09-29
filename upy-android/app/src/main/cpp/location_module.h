// upy-android: android.location, a thin layer over Android's LocationManager.
#ifndef UPY_ANDROID_LOCATION_MODULE_H
#define UPY_ANDROID_LOCATION_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

void location_bridge_init(void *jni_env);

// Wakes a waiting read(timeout_ms). Safe from any thread, like
// camera_interrupt_active_wait().
void location_interrupt_wait(void);

#ifdef __cplusplus
}
#endif

#endif
