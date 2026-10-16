// JNI-facing implementation for http (http_module.cpp). One JNI call each,
// through HttpShim.kt.

#include "http_jni_bridge.h"

#include <jni.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

// py/mphal.h, declared here because this file keeps MicroPython headers out.
extern "C" void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len);

namespace {

JavaVM *g_jvm = nullptr;
// Set when http_bridge_init_impl() has resolved everything. Interrupt can
// arrive on a Binder thread before that (Reset interrupts first, also right
// after the process starts), so cancel() must not use the IDs before then.
std::atomic<bool> g_ready{false};
jclass g_shim_class = nullptr;
jmethodID g_mid_begin = nullptr;
jmethodID g_mid_cancel = nullptr;
jmethodID g_mid_get = nullptr;
jmethodID g_mid_post = nullptr;
jmethodID g_mid_download = nullptr;

// The worker thread is JVM-attached for :engine's entire lifetime, and so is
// the Binder thread that calls Interrupt, so GetEnv() alone is enough.
JNIEnv *current_env() {
    JNIEnv *env = nullptr;
    g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    return env;
}

char *describe_and_clear_exception(JNIEnv *env) {
    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!exc) {
        return strdup("http: unknown error");
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

bool take_body(JNIEnv *env, jbyteArray body, uint8_t **out_data, size_t *out_len, char **out_err) {
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    jsize len = env->GetArrayLength(body);
    uint8_t *data = (uint8_t *) malloc(len > 0 ? len : 1);
    env->GetByteArrayRegion(body, 0, len, (jbyte *) data);
    env->DeleteLocalRef(body);
    *out_data = data;
    *out_len = (size_t) len;
    return true;
}

}  // namespace

extern "C" void http_bridge_init_impl(void *jni_env) {
    JNIEnv *env = (JNIEnv *) jni_env;
    env->GetJavaVM(&g_jvm);

    jclass local_shim = env->FindClass("eu/kdvelectronics/upyandroid/http/HttpShim");
    g_shim_class = (jclass) env->NewGlobalRef(local_shim);
    env->DeleteLocalRef(local_shim);

    g_mid_begin = env->GetStaticMethodID(g_shim_class, "begin", "()V");
    g_mid_cancel = env->GetStaticMethodID(g_shim_class, "cancel", "()V");
    g_mid_get = env->GetStaticMethodID(g_shim_class, "get", "(Ljava/lang/String;DJ)[B");
    g_mid_post = env->GetStaticMethodID(g_shim_class, "post", "(Ljava/lang/String;[BLjava/lang/String;DJ)[B");
    g_mid_download = env->GetStaticMethodID(g_shim_class, "download", "(Ljava/lang/String;Ljava/lang/String;ZD)V");
    g_ready.store(true, std::memory_order_release);
}

extern "C" void http_bridge_begin(void) {
    current_env()->CallStaticVoidMethod(g_shim_class, g_mid_begin);
}

extern "C" void http_bridge_cancel(void) {
    if (!g_ready.load(std::memory_order_acquire)) {
        return;
    }
    JNIEnv *env = current_env();
    if (env != nullptr) {
        env->CallStaticVoidMethod(g_shim_class, g_mid_cancel);
    }
}

extern "C" bool http_bridge_get(const char *url, double timeout_s, long long max_bytes,
                                 uint8_t **out_data, size_t *out_len, char **out_err) {
    JNIEnv *env = current_env();
    jstring jurl = env->NewStringUTF(url);
    auto body = (jbyteArray) env->CallStaticObjectMethod(g_shim_class, g_mid_get, jurl, (jdouble) timeout_s, (jlong) max_bytes);
    env->DeleteLocalRef(jurl);
    return take_body(env, body, out_data, out_len, out_err);
}

extern "C" bool http_bridge_post(const char *url, const uint8_t *data, size_t len, const char *content_type,
                                  double timeout_s, long long max_bytes,
                                  uint8_t **out_data, size_t *out_len, char **out_err) {
    JNIEnv *env = current_env();
    jstring jurl = env->NewStringUTF(url);
    jstring jtype = env->NewStringUTF(content_type);
    jbyteArray jdata = env->NewByteArray((jsize) len);
    env->SetByteArrayRegion(jdata, 0, (jsize) len, (const jbyte *) data);
    auto body = (jbyteArray) env->CallStaticObjectMethod(g_shim_class, g_mid_post, jurl, jdata, jtype,
                                                         (jdouble) timeout_s, (jlong) max_bytes);
    env->DeleteLocalRef(jurl);
    env->DeleteLocalRef(jtype);
    env->DeleteLocalRef(jdata);
    return take_body(env, body, out_data, out_len, out_err);
}

extern "C" bool http_bridge_download(const char *url, const char *path, bool progress, double timeout_s, char **out_err) {
    JNIEnv *env = current_env();
    jstring jurl = env->NewStringUTF(url);
    jstring jpath = env->NewStringUTF(path);
    env->CallStaticVoidMethod(g_shim_class, g_mid_download, jurl, jpath, (jboolean) progress, (jdouble) timeout_s);
    env->DeleteLocalRef(jurl);
    env->DeleteLocalRef(jpath);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

// HttpShim.nativePrint: progress text from download(), on the interpreter thread.
extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_http_HttpShim_nativePrint(JNIEnv *env, jclass, jstring text) {
    const char *chars = env->GetStringUTFChars(text, nullptr);
    mp_hal_stdout_tx_strn_cooked(chars, strlen(chars));
    env->ReleaseStringUTFChars(text, chars);
}
