// upy-android: JNI bridge from location_module.cpp to LocationShim.kt.
// Same structure as fileprovider_jni_bridge.cpp: class and method IDs
// looked up once at :engine init, calls made on the worker thread.
#include "location_jni_bridge.h"

#include <jni.h>

#include <cstring>

namespace {

JavaVM *g_jvm = nullptr;

jclass g_shim_class = nullptr;
jmethodID g_mid_start = nullptr;
jmethodID g_mid_stop = nullptr;
jmethodID g_mid_read_fix = nullptr;
jmethodID g_mid_read_provider = nullptr;

JNIEnv *current_env() {
    JNIEnv *env = nullptr;
    g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    return env;
}

}  // namespace

extern "C" void location_bridge_init_impl(void *jni_env) {
    JNIEnv *env = (JNIEnv *) jni_env;
    env->GetJavaVM(&g_jvm);

    jclass local_class = env->FindClass("eu/kdvelectronics/upyandroid/location/LocationShim");
    g_shim_class = (jclass) env->NewGlobalRef(local_class);
    env->DeleteLocalRef(local_class);

    g_mid_start = env->GetStaticMethodID(g_shim_class, "start", "(JF)I");
    g_mid_stop = env->GetStaticMethodID(g_shim_class, "stop", "()V");
    g_mid_read_fix = env->GetStaticMethodID(g_shim_class, "readFix", "(Z)[D");
    g_mid_read_provider = env->GetStaticMethodID(g_shim_class, "readProvider", "()Ljava/lang/String;");
}

extern "C" int location_bridge_start(long long interval_ms, float min_distance_m) {
    JNIEnv *env = current_env();
    jint result = env->CallStaticIntMethod(g_shim_class, g_mid_start, (jlong) interval_ms, (jfloat) min_distance_m);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return LOCATION_NO_PROVIDER;
    }
    return result;
}

extern "C" void location_bridge_stop(void) {
    JNIEnv *env = current_env();
    env->CallStaticVoidMethod(g_shim_class, g_mid_stop);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

extern "C" bool location_bridge_read(bool last_known, double *fix, char *provider, int provider_len) {
    JNIEnv *env = current_env();
    jdoubleArray arr = (jdoubleArray) env->CallStaticObjectMethod(g_shim_class, g_mid_read_fix, (jboolean) last_known);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    if (arr == nullptr) {
        return false;
    }
    env->GetDoubleArrayRegion(arr, 0, 7, fix);
    env->DeleteLocalRef(arr);

    provider[0] = '\0';
    jstring jprovider = (jstring) env->CallStaticObjectMethod(g_shim_class, g_mid_read_provider);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (jprovider != nullptr) {
        const char *chars = env->GetStringUTFChars(jprovider, nullptr);
        strncpy(provider, chars, provider_len - 1);
        provider[provider_len - 1] = '\0';
        env->ReleaseStringUTFChars(jprovider, chars);
        env->DeleteLocalRef(jprovider);
    }
    return true;
}
