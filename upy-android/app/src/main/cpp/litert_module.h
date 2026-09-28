// upy-android native litert module.
// micropython android port only.
#ifndef UPY_ANDROID_LITERT_MODULE_H
#define UPY_ANDROID_LITERT_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Called once, from engine_jni.cpp's nativeInit(), after g_jvm is
// cached. Resolves and caches LiteRtShim's class and every jmethodID
// it needs, once, so no per-call FindClass()/GetStaticMethodID() cost.
// jni_env is a JNIEnv*, typed void* here so this header (included by
// litert_module.cpp, which is qstr-scanned and must not include
// <jni.h>) never needs the real JNI types.
void litert_bridge_init(void *jni_env);

// Idempotent, safe to call when nothing is open. Same tier/contract as
// camera_close_all()/imu_close_all()/mqtt_close_all(), called from the
// same two places in engine_jni.cpp, before mp_embed_deinit().
// Destroys every registered TensorBuffer/CompiledModel first, then
// every Environment last.
// see session-state: litert_module.cpp#registry_design
void litert_close_all(void);

#ifdef __cplusplus
}
#endif

#endif
