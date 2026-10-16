// upy-android native zipfile module.
// micropython android port only.
#ifndef UPY_ANDROID_ZIPFILE_MODULE_H
#define UPY_ANDROID_ZIPFILE_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Called once, from engine_jni.cpp's nativeInit(), after g_jvm is cached.
// Resolves ZipShim's class and method IDs. jni_env is a JNIEnv*, typed
// void* like mqtt_module.h.
void zipfile_bridge_init(void *jni_env);

#ifdef __cplusplus
}
#endif

#endif
