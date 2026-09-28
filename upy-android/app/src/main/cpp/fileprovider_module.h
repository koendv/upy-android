// upy-android native fileprovider module.
// micropython android port only.
#ifndef UPY_ANDROID_FILEPROVIDER_MODULE_H
#define UPY_ANDROID_FILEPROVIDER_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Called once, from engine_jni.cpp's nativeInit(), after g_jvm is
// cached. Resolves and caches EngineService's static requestShare
// method ID. jni_env is a JNIEnv*, typed void* here to stay consistent
// with mediastore_module.h/litert_module.h's own no-real-JNI-types
// convention.
void fileprovider_bridge_init(void *jni_env);

#ifdef __cplusplus
}
#endif

#endif
