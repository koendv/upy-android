// upy-android native http module.
// micropython android port only.
#ifndef UPY_ANDROID_HTTP_MODULE_H
#define UPY_ANDROID_HTTP_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Called once, from engine_jni.cpp's nativeInit(), after g_jvm is cached.
// Resolves HttpShim's class and method IDs. jni_env is a JNIEnv*, typed void*
// like mqtt_module.h.
void http_bridge_init(void *jni_env);

// Any thread: ends the running http call (Interrupt). Safe when none runs.
void http_interrupt(void);

#ifdef __cplusplus
}
#endif

#endif
