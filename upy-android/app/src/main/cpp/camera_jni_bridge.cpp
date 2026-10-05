// JNI-facing implementation for the camera module, one JNI call each,
// through CameraShim.kt.

#include "camera_jni_bridge.h"

#include <jni.h>

#include <cstdlib>
#include <cstring>

namespace {

JavaVM *g_jvm = nullptr;
jclass g_shim_class = nullptr;

jmethodID g_mid_count = nullptr;
jmethodID g_mid_id = nullptr;
jmethodID g_mid_facing = nullptr;
jmethodID g_mid_sizes = nullptr;
jmethodID g_mid_open = nullptr;
jmethodID g_mid_open_info = nullptr;
jmethodID g_mid_buffer = nullptr;
jmethodID g_mid_acquire = nullptr;
jmethodID g_mid_release = nullptr;
jmethodID g_mid_close = nullptr;
jmethodID g_mid_interrupt = nullptr;

// Null on a thread not attached to the JVM.
JNIEnv *current_env() {
    JNIEnv *env = nullptr;
    if (!g_jvm || g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        return nullptr;
    }
    return env;
}

// Same as mqtt_jni_bridge.cpp's.
char *describe_and_clear_exception(JNIEnv *env) {
    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!exc) {
        return strdup("camera: unknown error");
    }
    // IOExceptions from CameraShim carry a ready message; anything else
    // gets its class name too.
    jclass io_cls = env->FindClass("java/io/IOException");
    bool is_io = env->IsInstanceOf(exc, io_cls);
    env->DeleteLocalRef(io_cls);
    jclass throwable_cls = env->FindClass("java/lang/Throwable");
    jmethodID mid = env->GetMethodID(throwable_cls, is_io ? "getMessage" : "toString", "()Ljava/lang/String;");
    auto msg = (jstring) env->CallObjectMethod(exc, mid);
    env->ExceptionClear();
    char *out;
    if (msg) {
        const char *chars = env->GetStringUTFChars(msg, nullptr);
        out = strdup(chars);
        env->ReleaseStringUTFChars(msg, chars);
        env->DeleteLocalRef(msg);
    } else {
        out = strdup("camera: unknown error");
    }
    env->DeleteLocalRef(throwable_cls);
    env->DeleteLocalRef(exc);
    return out;
}

bool check(JNIEnv *env, char **out_err) {
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

char *dup_jstring(JNIEnv *env, jstring s) {
    const char *chars = env->GetStringUTFChars(s, nullptr);
    char *out = strdup(chars);
    env->ReleaseStringUTFChars(s, chars);
    return out;
}

bool get_string(jmethodID mid, int index, char **out, char **out_err) {
    JNIEnv *env = current_env();
    auto s = (jstring) env->CallStaticObjectMethod(g_shim_class, mid, (jint) index);
    if (!check(env, out_err)) {
        return false;
    }
    *out = dup_jstring(env, s);
    env->DeleteLocalRef(s);
    return true;
}

}  // namespace

extern "C" void camera_bridge_init_impl(void *jni_env) {
    auto *env = (JNIEnv *) jni_env;
    env->GetJavaVM(&g_jvm);

    jclass local = env->FindClass("eu/kdvelectronics/upyandroid/camera/CameraShim");
    g_shim_class = (jclass) env->NewGlobalRef(local);
    env->DeleteLocalRef(local);

    g_mid_count = env->GetStaticMethodID(g_shim_class, "count", "()I");
    g_mid_id = env->GetStaticMethodID(g_shim_class, "id", "(I)Ljava/lang/String;");
    g_mid_facing = env->GetStaticMethodID(g_shim_class, "facing", "(I)Ljava/lang/String;");
    g_mid_sizes = env->GetStaticMethodID(g_shim_class, "sizes", "(I)[I");
    g_mid_open = env->GetStaticMethodID(g_shim_class, "open", "(Ljava/lang/String;II)I");
    g_mid_open_info = env->GetStaticMethodID(g_shim_class, "openInfo", "()[I");
    g_mid_buffer = env->GetStaticMethodID(g_shim_class, "buffer", "(I)Ljava/nio/ByteBuffer;");
    g_mid_acquire = env->GetStaticMethodID(g_shim_class, "acquire", "(JJ)[J");
    g_mid_release = env->GetStaticMethodID(g_shim_class, "release", "()V");
    g_mid_close = env->GetStaticMethodID(g_shim_class, "close", "()V");
    g_mid_interrupt = env->GetStaticMethodID(g_shim_class, "interrupt", "()V");
}

extern "C" bool camera_bridge_count(int *out_count, char **out_err) {
    JNIEnv *env = current_env();
    jint n = env->CallStaticIntMethod(g_shim_class, g_mid_count);
    if (!check(env, out_err)) {
        return false;
    }
    *out_count = n;
    return true;
}

extern "C" bool camera_bridge_id(int index, char **out_id, char **out_err) {
    return get_string(g_mid_id, index, out_id, out_err);
}

extern "C" bool camera_bridge_facing(int index, char **out_facing, char **out_err) {
    return get_string(g_mid_facing, index, out_facing, out_err);
}

extern "C" bool camera_bridge_sizes(int index, int32_t **out_sizes, size_t *out_n_sizes, char **out_err) {
    JNIEnv *env = current_env();
    auto arr = (jintArray) env->CallStaticObjectMethod(g_shim_class, g_mid_sizes, (jint) index);
    if (!check(env, out_err)) {
        return false;
    }
    jsize len = env->GetArrayLength(arr);
    auto *buf = (int32_t *) malloc(sizeof(int32_t) * (len > 0 ? len : 1));
    env->GetIntArrayRegion(arr, 0, len, (jint *) buf);
    env->DeleteLocalRef(arr);
    *out_sizes = buf;
    *out_n_sizes = (size_t) len / 2;
    return true;
}

extern "C" bool camera_bridge_open(const char *id, int width, int height, int *out_status, char **out_err) {
    JNIEnv *env = current_env();
    jstring jid = id ? env->NewStringUTF(id) : nullptr;
    jint status = env->CallStaticIntMethod(g_shim_class, g_mid_open, jid, (jint) width, (jint) height);
    if (jid) {
        env->DeleteLocalRef(jid);
    }
    if (!check(env, out_err)) {
        return false;
    }
    *out_status = status;
    return true;
}

extern "C" bool camera_bridge_open_info(int32_t out[5], char **out_err) {
    JNIEnv *env = current_env();
    auto arr = (jintArray) env->CallStaticObjectMethod(g_shim_class, g_mid_open_info);
    if (!check(env, out_err)) {
        return false;
    }
    env->GetIntArrayRegion(arr, 0, 5, (jint *) out);
    env->DeleteLocalRef(arr);
    return true;
}

extern "C" bool camera_bridge_buffer(int index, uint8_t **out_data, size_t *out_size, char **out_err) {
    JNIEnv *env = current_env();
    jobject buf = env->CallStaticObjectMethod(g_shim_class, g_mid_buffer, (jint) index);
    if (!check(env, out_err)) {
        return false;
    }
    *out_data = (uint8_t *) env->GetDirectBufferAddress(buf);
    *out_size = (size_t) env->GetDirectBufferCapacity(buf);
    env->DeleteLocalRef(buf);
    if (!*out_data) {
        *out_err = strdup("camera: frame buffer is not a direct buffer");
        return false;
    }
    return true;
}

extern "C" bool camera_bridge_acquire(int64_t last_seq, long timeout_ms, bool *out_has_frame,
                                      camera_frame_t *out, char **out_err) {
    JNIEnv *env = current_env();
    auto arr = (jlongArray) env->CallStaticObjectMethod(g_shim_class, g_mid_acquire,
                                                        (jlong) last_seq, (jlong) timeout_ms);
    if (!check(env, out_err)) {
        return false;
    }
    if (!arr) {
        *out_has_frame = false;
        return true;
    }
    jlong v[6];
    env->GetLongArrayRegion(arr, 0, 6, v);
    env->DeleteLocalRef(arr);
    out->slot = (int) v[0];
    out->seq = v[1];
    out->timestamp_ns = v[2];
    out->width = (int32_t) v[3];
    out->height = (int32_t) v[4];
    out->rotation_degrees = (int32_t) v[5];
    *out_has_frame = true;
    return true;
}

extern "C" void camera_bridge_release(void) {
    JNIEnv *env = current_env();
    env->CallStaticVoidMethod(g_shim_class, g_mid_release);
    env->ExceptionClear();
}

extern "C" bool camera_bridge_close(char **out_err) {
    JNIEnv *env = current_env();
    env->CallStaticVoidMethod(g_shim_class, g_mid_close);
    return check(env, out_err);
}

extern "C" void camera_bridge_interrupt(void) {
    JNIEnv *env = current_env();
    if (!env || !g_shim_class) {
        return;
    }
    env->CallStaticVoidMethod(g_shim_class, g_mid_interrupt);
    env->ExceptionClear();
}
