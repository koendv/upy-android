// upy-android native mediastore module. OUR OWN code, NOT vendored
// OpenMV source.
#ifndef UPY_ANDROID_MEDIASTORE_MODULE_H
#define UPY_ANDROID_MEDIASTORE_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Called once, from engine_jni.cpp's nativeInit(), after g_jvm is
// cached -- resolves and caches MediaStoreShim's class/method, and
// holds a global ref to the application Context passed in. jni_env/
// application_context are JNIEnv*/jobject -- typed void* here so this
// header (included by engine_jni.cpp, which does have <jni.h>, but
// kept symmetric with litert_module.h/mediastore_jni_bridge.h's own
// no-real-JNI-types convention) stays consistent project-wide.
void mediastore_bridge_init(void *jni_env, void *application_context);

#ifdef __cplusplus
}
#endif

#endif
