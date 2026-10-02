// upy-android native tflite module.
// micropython android port only.
#ifndef UPY_ANDROID_TFLITE_MODULE_H
#define UPY_ANDROID_TFLITE_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Idempotent, safe to call when nothing is open. Same tier/contract as
// litert_close_all()/camera_close_all()/imu_close_all()/mqtt_close_all(),
// called from the same two places in engine_jni.cpp, before
// mp_embed_deinit(). Destroys every registered TfLiteInterpreter/
// TfLiteModel.
void tflite_close_all(void);

#ifdef __cplusplus
}
#endif

#endif
