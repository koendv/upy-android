// JNI-facing implementation for zipfile (zipfile_module.cpp). One JNI call
// each, through ZipShim.kt.

#include "zipfile_jni_bridge.h"

#include <jni.h>

#include <cstdlib>
#include <cstring>

namespace {

JavaVM *g_jvm = nullptr;
jclass g_shim_class = nullptr;
jmethodID g_mid_info = nullptr;
jmethodID g_mid_extract_all = nullptr;
jmethodID g_mid_create = nullptr;

// The worker thread is JVM-attached for :engine's entire lifetime, so
// GetEnv() alone is enough, same as mqtt_jni_bridge.cpp.
JNIEnv *current_env() {
    JNIEnv *env = nullptr;
    g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    return env;
}

char *describe_and_clear_exception(JNIEnv *env) {
    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!exc) {
        return strdup("zipfile: unknown error");
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

extern "C" void zipfile_bridge_init_impl(void *jni_env) {
    JNIEnv *env = (JNIEnv *) jni_env;
    env->GetJavaVM(&g_jvm);

    jclass local_shim = env->FindClass("eu/kdvelectronics/upyandroid/zip/ZipShim");
    g_shim_class = (jclass) env->NewGlobalRef(local_shim);
    env->DeleteLocalRef(local_shim);

    g_mid_info = env->GetStaticMethodID(g_shim_class, "info", "(Ljava/lang/String;)[Ljava/lang/String;");
    g_mid_extract_all = env->GetStaticMethodID(g_shim_class, "extractAll", "(Ljava/lang/String;Ljava/lang/String;)V");
    g_mid_create = env->GetStaticMethodID(g_shim_class, "create", "(Ljava/lang/String;Ljava/lang/String;)V");
}

extern "C" bool zipfile_bridge_info(const char *zip, char ***out_entries, size_t *out_count, char **out_err) {
    JNIEnv *env = current_env();
    jstring jzip = env->NewStringUTF(zip);
    auto entries = (jobjectArray) env->CallStaticObjectMethod(g_shim_class, g_mid_info, jzip);
    env->DeleteLocalRef(jzip);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    jsize count = env->GetArrayLength(entries);
    char **list = (char **) malloc(sizeof(char *) * (count > 0 ? count : 1));
    for (jsize i = 0; i < count; i++) {
        auto jentry = (jstring) env->GetObjectArrayElement(entries, i);
        const char *chars = env->GetStringUTFChars(jentry, nullptr);
        list[i] = strdup(chars);
        env->ReleaseStringUTFChars(jentry, chars);
        env->DeleteLocalRef(jentry);
    }
    env->DeleteLocalRef(entries);
    *out_entries = list;
    *out_count = (size_t) count;
    return true;
}

namespace {

bool call_two_strings(jmethodID method, const char *a, const char *b, char **out_err) {
    JNIEnv *env = current_env();
    jstring ja = env->NewStringUTF(a);
    jstring jb = env->NewStringUTF(b);
    env->CallStaticVoidMethod(g_shim_class, method, ja, jb);
    env->DeleteLocalRef(ja);
    env->DeleteLocalRef(jb);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

}  // namespace

extern "C" bool zipfile_bridge_read(const char *zip, const char *dest, char **out_err) {
    return call_two_strings(g_mid_extract_all, zip, dest, out_err);
}

extern "C" bool zipfile_bridge_write(const char *zip, const char *path, char **out_err) {
    return call_two_strings(g_mid_create, zip, path, out_err);
}
