// JNI-facing implementation for android.mediastore
// (mediastore_module.cpp). One JNI call, through MediaStoreShim.kt --
// see mediastore_jni_bridge.h's own header comment for why
// mediastore_module.cpp never sees a real jobject/JNIEnv*.

#include "mediastore_jni_bridge.h"

#include <jni.h>

#include <cstdlib>
#include <cstring>

namespace {

JavaVM *g_jvm = nullptr;
jobject g_context = nullptr;  // global ref, the application Context

jclass g_shim_class = nullptr;
jmethodID g_mid_save_image = nullptr;

// Same per-call JNIEnv* lookup as litert_jni_bridge.cpp's own
// current_env() -- the worker thread is JVM-attached for :engine's
// entire lifetime, so GetEnv() alone is enough, no Attach/Detach.
// see session-state: litert_jni_bridge.cpp#threading
JNIEnv *current_env() {
    JNIEnv *env = nullptr;
    g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    return env;
}

// Same exception-to-string pattern as litert_jni_bridge.cpp's own
// describe_and_clear_exception() -- ExceptionClear() must happen
// before any further JNI call and before mediastore_module.cpp's
// raise_os_error() (which longjmps via nlr_raise()).
char *describe_and_clear_exception(JNIEnv *env) {
    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!exc) {
        return strdup("android.mediastore: unknown error");
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

extern "C" void mediastore_bridge_init_impl(void *jni_env, void *application_context) {
    JNIEnv *env = (JNIEnv *) jni_env;
    env->GetJavaVM(&g_jvm);
    g_context = env->NewGlobalRef((jobject) application_context);

    jclass local_shim = env->FindClass("eu/kdvelectronics/upyandroid/mediastore/MediaStoreShim");
    g_shim_class = (jclass) env->NewGlobalRef(local_shim);
    env->DeleteLocalRef(local_shim);

    g_mid_save_image = env->GetStaticMethodID(g_shim_class, "saveImage",
        "(Landroid/content/Context;[BLjava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
}

extern "C" bool mediastore_bridge_save_image(const uint8_t *data, size_t len,
                                              const char *display_name, const char *mime_type,
                                              char **out_uri, char **out_err) {
    JNIEnv *env = current_env();

    jbyteArray jdata = env->NewByteArray((jsize) len);
    env->SetByteArrayRegion(jdata, 0, (jsize) len, (const jbyte *) data);
    jstring jname = env->NewStringUTF(display_name);
    jstring jmime = env->NewStringUTF(mime_type);

    auto juri = (jstring) env->CallStaticObjectMethod(g_shim_class, g_mid_save_image,
                                                        g_context, jdata, jname, jmime);

    env->DeleteLocalRef(jdata);
    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(jmime);

    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }

    const char *uri_chars = env->GetStringUTFChars(juri, nullptr);
    *out_uri = strdup(uri_chars);
    env->ReleaseStringUTFChars(juri, uri_chars);
    env->DeleteLocalRef(juri);
    return true;
}
