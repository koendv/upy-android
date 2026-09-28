// JNI-facing implementation for android.fileprovider
// (fileprovider_module.cpp). One JNI call, straight into
// EngineService.requestShare(). See fileprovider_jni_bridge.h's own
// header comment for why fileprovider_module.cpp never sees a real
// jobject/JNIEnv*.

#include "fileprovider_jni_bridge.h"

#include <jni.h>

#include <cstdlib>
#include <cstring>

namespace {

JavaVM *g_jvm = nullptr;

jclass g_engine_service_class = nullptr;
jmethodID g_mid_request_share = nullptr;

// Same per-call JNIEnv* lookup as mediastore_jni_bridge.cpp's own
// current_env(). The worker thread is JVM-attached for :engine's
// entire lifetime, so GetEnv() alone is enough, no Attach/Detach.
JNIEnv *current_env() {
    JNIEnv *env = nullptr;
    g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    return env;
}

// Same exception-to-string pattern as mediastore_jni_bridge.cpp's own
// describe_and_clear_exception().
char *describe_and_clear_exception(JNIEnv *env) {
    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!exc) {
        return strdup("android.fileprovider: unknown error");
    }
    jclass throwable_cls = env->FindClass("java/lang/Throwable");
    jmethodID to_string_mid = env->GetMethodID(throwable_cls, "toString", "()Ljava/lang/String;");
    jstring msg = (jstring) env->CallObjectMethod(exc, to_string_mid);
    const char *chars = env->GetStringUTFChars(msg, nullptr);
    char *out = strdup(chars);
    env->ReleaseStringUTFChars(msg, chars);
    env->DeleteLocalRef(msg);
    env->DeleteLocalRef(throwable_cls);
    env->DeleteLocalRef(exc);
    return out;
}

}  // namespace

extern "C" void fileprovider_bridge_init_impl(void *jni_env) {
    JNIEnv *env = (JNIEnv *) jni_env;
    env->GetJavaVM(&g_jvm);

    jclass local_class = env->FindClass("eu/kdvelectronics/upyandroid/EngineService");
    g_engine_service_class = (jclass) env->NewGlobalRef(local_class);
    env->DeleteLocalRef(local_class);

    // requestShare is a companion-object @JvmStatic member, which
    // compiles to a genuine static method directly on EngineService.
    g_mid_request_share = env->GetStaticMethodID(g_engine_service_class, "requestShare",
        "(Ljava/lang/String;Ljava/lang/String;)V");
}

extern "C" bool fileprovider_bridge_share(const char *path, const char *mime_type, char **out_err) {
    JNIEnv *env = current_env();

    jstring jpath = env->NewStringUTF(path);
    jstring jmime = env->NewStringUTF(mime_type);

    env->CallStaticVoidMethod(g_engine_service_class, g_mid_request_share, jpath, jmime);

    env->DeleteLocalRef(jpath);
    env->DeleteLocalRef(jmime);

    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}
