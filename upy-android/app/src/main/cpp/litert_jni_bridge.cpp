// JNI-facing implementation for litert (litert_module.cpp). One JNI
// call each, through LiteRtShim.kt. See litert_jni_bridge.h's own
// header comment for why litert_module.cpp never sees a real
// jobject/JNIEnv*.
// see session-state: litert_jni_bridge.cpp#hot_path_mechanism

#include "litert_jni_bridge.h"

#include <jni.h>

#include <cstdlib>
#include <cstring>

namespace {

JavaVM *g_jvm = nullptr;

jclass g_shim_class = nullptr;
jclass g_jnihandle_class = nullptr;
jfieldID g_handle_field = nullptr;

jmethodID g_mid_create_environment = nullptr;
jmethodID g_mid_create_compiled_model = nullptr;
jmethodID g_mid_create_input_buffers = nullptr;
jmethodID g_mid_create_output_buffers = nullptr;
jmethodID g_mid_write_int8 = nullptr;
jmethodID g_mid_read_int8 = nullptr;
jmethodID g_mid_write_float = nullptr;
jmethodID g_mid_read_float = nullptr;
jmethodID g_mid_write_int = nullptr;
jmethodID g_mid_read_int = nullptr;
jmethodID g_mid_write_bool = nullptr;
jmethodID g_mid_read_bool = nullptr;
jmethodID g_mid_write_long = nullptr;
jmethodID g_mid_read_long = nullptr;
jmethodID g_mid_run = nullptr;
jmethodID g_mid_close_environment = nullptr;
jmethodID g_mid_close_compiled_model = nullptr;
jmethodID g_mid_close_tensor_buffer = nullptr;

// see session-state: litert_jni_bridge.cpp#threading
JNIEnv *current_env() {
    JNIEnv *env = nullptr;
    g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    return env;
}

// Applied after every Call*Method/CallStatic*Method in this file, no
// exceptions. ExceptionClear() must happen before any further JNI call
// and before litert_module.cpp's raise_os_error() (which longjmps via
// nlr_raise()). A pending Java exception surviving a longjmp corrupts
// JVM state.
char *describe_and_clear_exception(JNIEnv *env) {
    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!exc) {
        return strdup("litert: unknown error");
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

// Extracts JniHandle's raw native handle from a real litert-api object
// (Environment/CompiledModel/TensorBuffer all extend JniHandle). The
// fieldID was resolved from JniHandle itself, which JNI guarantees
// works across that class's subclasses, no per-instance FindClass
// needed. GetFieldID/GetLongField operate on the compiled classfile
// directly and are blind to Kotlin's compiler-level `internal`
// visibility check, which is why this extraction lives here, in C++,
// and not in LiteRtShim.kt.
long extract_handle(JNIEnv *env, jobject obj) {
    return (long) env->GetLongField(obj, g_handle_field);
}

}  // namespace

extern "C" void litert_bridge_init_impl(void *jni_env) {
    JNIEnv *env = (JNIEnv *) jni_env;
    env->GetJavaVM(&g_jvm);

    jclass local_shim = env->FindClass("eu/kdvelectronics/upyandroid/litert/LiteRtShim");
    g_shim_class = (jclass) env->NewGlobalRef(local_shim);
    env->DeleteLocalRef(local_shim);

    jclass local_jh = env->FindClass("com/google/ai/edge/litert/JniHandle");
    g_jnihandle_class = (jclass) env->NewGlobalRef(local_jh);
    env->DeleteLocalRef(local_jh);
    g_handle_field = env->GetFieldID(g_jnihandle_class, "handle", "J");

    g_mid_create_environment = env->GetStaticMethodID(g_shim_class,
        "createEnvironment", "()Lcom/google/ai/edge/litert/Environment;");
    g_mid_create_compiled_model = env->GetStaticMethodID(g_shim_class,
        "createCompiledModel",
        "(Lcom/google/ai/edge/litert/Environment;Ljava/lang/String;I)"
        "Lcom/google/ai/edge/litert/CompiledModel;");
    g_mid_create_input_buffers = env->GetStaticMethodID(g_shim_class,
        "createInputBuffers",
        "(Lcom/google/ai/edge/litert/CompiledModel;)[Lcom/google/ai/edge/litert/TensorBuffer;");
    g_mid_create_output_buffers = env->GetStaticMethodID(g_shim_class,
        "createOutputBuffers",
        "(Lcom/google/ai/edge/litert/CompiledModel;)[Lcom/google/ai/edge/litert/TensorBuffer;");
    g_mid_write_int8 = env->GetStaticMethodID(g_shim_class,
        "writeInt8", "(Lcom/google/ai/edge/litert/TensorBuffer;[B)V");
    g_mid_read_int8 = env->GetStaticMethodID(g_shim_class,
        "readInt8", "(Lcom/google/ai/edge/litert/TensorBuffer;)[B");
    g_mid_write_float = env->GetStaticMethodID(g_shim_class,
        "writeFloat", "(Lcom/google/ai/edge/litert/TensorBuffer;[F)V");
    g_mid_read_float = env->GetStaticMethodID(g_shim_class,
        "readFloat", "(Lcom/google/ai/edge/litert/TensorBuffer;)[F");
    g_mid_write_int = env->GetStaticMethodID(g_shim_class,
        "writeInt", "(Lcom/google/ai/edge/litert/TensorBuffer;[I)V");
    g_mid_read_int = env->GetStaticMethodID(g_shim_class,
        "readInt", "(Lcom/google/ai/edge/litert/TensorBuffer;)[I");
    g_mid_write_bool = env->GetStaticMethodID(g_shim_class,
        "writeBool", "(Lcom/google/ai/edge/litert/TensorBuffer;[Z)V");
    g_mid_read_bool = env->GetStaticMethodID(g_shim_class,
        "readBool", "(Lcom/google/ai/edge/litert/TensorBuffer;)[Z");
    g_mid_write_long = env->GetStaticMethodID(g_shim_class,
        "writeLong", "(Lcom/google/ai/edge/litert/TensorBuffer;[J)V");
    g_mid_read_long = env->GetStaticMethodID(g_shim_class,
        "readLong", "(Lcom/google/ai/edge/litert/TensorBuffer;)[J");
    g_mid_run = env->GetStaticMethodID(g_shim_class,
        "run",
        "(Lcom/google/ai/edge/litert/CompiledModel;"
        "[Lcom/google/ai/edge/litert/TensorBuffer;"
        "[Lcom/google/ai/edge/litert/TensorBuffer;)V");
    g_mid_close_environment = env->GetStaticMethodID(g_shim_class,
        "closeEnvironment", "(Lcom/google/ai/edge/litert/Environment;)V");
    g_mid_close_compiled_model = env->GetStaticMethodID(g_shim_class,
        "closeCompiledModel", "(Lcom/google/ai/edge/litert/CompiledModel;)V");
    g_mid_close_tensor_buffer = env->GetStaticMethodID(g_shim_class,
        "closeTensorBuffer", "(Lcom/google/ai/edge/litert/TensorBuffer;)V");
    // Any null jfieldID/jmethodID above is a packaging/signature bug.
    // Caught at first use (a pending NoSuchFieldError/NoSuchMethodError
    // surfaces through describe_and_clear_exception() the moment
    // something calls it), not silently ignored here.
}

extern "C" bool litert_bridge_create_environment(long *out_handle, void **out_global_ref,
                                                  char **out_err) {
    JNIEnv *env = current_env();
    jobject local = env->CallStaticObjectMethod(g_shim_class, g_mid_create_environment);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    *out_handle = extract_handle(env, local);
    *out_global_ref = env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    return true;
}

extern "C" bool litert_bridge_create_compiled_model(void *env_global_ref, const char *path,
                                                     int accelerator_value, long *out_handle,
                                                     void **out_global_ref, char **out_err) {
    JNIEnv *env = current_env();
    jstring jpath = env->NewStringUTF(path);
    jobject local = env->CallStaticObjectMethod(g_shim_class, g_mid_create_compiled_model,
                                                 (jobject) env_global_ref, jpath,
                                                 (jint) accelerator_value);
    env->DeleteLocalRef(jpath);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    *out_handle = extract_handle(env, local);
    *out_global_ref = env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    return true;
}

namespace {

// see session-state: litert_jni_bridge.h#litert_bridge_kotlin_read_int8
bool create_buffers(jmethodID mid, void *model_global_ref, long **out_handles,
                     void ***out_global_refs, size_t *out_count, char **out_err) {
    JNIEnv *env = current_env();
    auto local_array = (jobjectArray) env->CallStaticObjectMethod(
        g_shim_class, mid, (jobject) model_global_ref);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    jsize count = env->GetArrayLength(local_array);
    auto *handles = (long *) malloc(sizeof(long) * (size_t) count);
    auto *global_refs = (void **) malloc(sizeof(void *) * (size_t) count);
    for (jsize i = 0; i < count; i++) {
        jobject local_buf = env->GetObjectArrayElement(local_array, i);
        handles[i] = extract_handle(env, local_buf);
        global_refs[i] = env->NewGlobalRef(local_buf);
        env->DeleteLocalRef(local_buf);
    }
    env->DeleteLocalRef(local_array);
    *out_handles = handles;
    *out_global_refs = global_refs;
    *out_count = (size_t) count;
    return true;
}

}  // namespace

extern "C" bool litert_bridge_create_input_buffers(void *model_global_ref, long **out_handles,
                                                    void ***out_global_refs,
                                                    size_t *out_count, char **out_err) {
    return create_buffers(g_mid_create_input_buffers, model_global_ref, out_handles,
                           out_global_refs, out_count, out_err);
}

extern "C" bool litert_bridge_create_output_buffers(void *model_global_ref, long **out_handles,
                                                     void ***out_global_refs,
                                                     size_t *out_count, char **out_err) {
    return create_buffers(g_mid_create_output_buffers, model_global_ref, out_handles,
                           out_global_refs, out_count, out_err);
}

extern "C" bool litert_bridge_kotlin_write_int8(void *buf_global_ref, const int8_t *data,
                                                 size_t len, char **out_err) {
    JNIEnv *env = current_env();
    jbyteArray jdata = env->NewByteArray((jsize) len);
    env->SetByteArrayRegion(jdata, 0, (jsize) len, (const jbyte *) data);
    env->CallStaticVoidMethod(g_shim_class, g_mid_write_int8, (jobject) buf_global_ref, jdata);
    env->DeleteLocalRef(jdata);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

// see session-state: litert_jni_bridge.h#litert_bridge_kotlin_read_int8
extern "C" bool litert_bridge_kotlin_read_int8(void *buf_global_ref, int8_t **out_data,
                                                size_t *out_len, char **out_err) {
    JNIEnv *env = current_env();
    auto jresult = (jbyteArray) env->CallStaticObjectMethod(g_shim_class, g_mid_read_int8,
                                                             (jobject) buf_global_ref);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    jsize len = env->GetArrayLength(jresult);
    auto *data = (int8_t *) malloc((size_t) len);
    env->GetByteArrayRegion(jresult, 0, len, (jbyte *) data);
    env->DeleteLocalRef(jresult);
    *out_data = data;
    *out_len = (size_t) len;
    return true;
}

// write_ndarray()'s float32 path, a real typed TensorBuffer.writeFloat(),
// not a byte-reinterpret through writeInt8. num_elements is a count of
// floats, not bytes.
extern "C" bool litert_bridge_kotlin_write_float(void *buf_global_ref, const float *data,
                                                  size_t num_elements, char **out_err) {
    JNIEnv *env = current_env();
    jfloatArray jdata = env->NewFloatArray((jsize) num_elements);
    env->SetFloatArrayRegion(jdata, 0, (jsize) num_elements, data);
    env->CallStaticVoidMethod(g_shim_class, g_mid_write_float, (jobject) buf_global_ref, jdata);
    env->DeleteLocalRef(jdata);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

// read_ndarray()'s float32 path, same length-from-real-array reasoning
// as litert_bridge_kotlin_read_int8 above. *out_data is a malloc'd
// buffer of *out_len FLOATS (not bytes), owned by the caller.
extern "C" bool litert_bridge_kotlin_read_float(void *buf_global_ref, float **out_data,
                                                 size_t *out_len, char **out_err) {
    JNIEnv *env = current_env();
    auto jresult = (jfloatArray) env->CallStaticObjectMethod(g_shim_class, g_mid_read_float,
                                                              (jobject) buf_global_ref);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    jsize len = env->GetArrayLength(jresult);
    auto *data = (float *) malloc((size_t) len * sizeof(float));
    env->GetFloatArrayRegion(jresult, 0, len, data);
    env->DeleteLocalRef(jresult);
    *out_data = data;
    *out_len = (size_t) len;
    return true;
}

extern "C" bool litert_bridge_kotlin_write_int(void *buf_global_ref, const int32_t *data,
                                                size_t num_elements, char **out_err) {
    JNIEnv *env = current_env();
    jintArray jdata = env->NewIntArray((jsize) num_elements);
    env->SetIntArrayRegion(jdata, 0, (jsize) num_elements, (const jint *) data);
    env->CallStaticVoidMethod(g_shim_class, g_mid_write_int, (jobject) buf_global_ref, jdata);
    env->DeleteLocalRef(jdata);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

extern "C" bool litert_bridge_kotlin_read_int(void *buf_global_ref, int32_t **out_data,
                                               size_t *out_len, char **out_err) {
    JNIEnv *env = current_env();
    auto jresult = (jintArray) env->CallStaticObjectMethod(g_shim_class, g_mid_read_int,
                                                            (jobject) buf_global_ref);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    jsize len = env->GetArrayLength(jresult);
    auto *data = (int32_t *) malloc((size_t) len * sizeof(int32_t));
    env->GetIntArrayRegion(jresult, 0, len, (jint *) data);
    env->DeleteLocalRef(jresult);
    *out_data = data;
    *out_len = (size_t) len;
    return true;
}

// bool <-> jboolean marshaled explicitly (via a temporary jboolean
// array) rather than reinterpret-casting a bool* directly. jboolean is
// an unsigned char with JNI_TRUE/JNI_FALSE values; C++ bool's
// representation is not guaranteed identical, so this avoids relying
// on that.
extern "C" bool litert_bridge_kotlin_write_bool(void *buf_global_ref, const bool *data,
                                                 size_t num_elements, char **out_err) {
    JNIEnv *env = current_env();
    auto *jdata_tmp = (jboolean *) malloc(sizeof(jboolean) * num_elements);
    for (size_t i = 0; i < num_elements; i++) {
        jdata_tmp[i] = data[i] ? JNI_TRUE : JNI_FALSE;
    }
    jbooleanArray jdata = env->NewBooleanArray((jsize) num_elements);
    env->SetBooleanArrayRegion(jdata, 0, (jsize) num_elements, jdata_tmp);
    free(jdata_tmp);
    env->CallStaticVoidMethod(g_shim_class, g_mid_write_bool, (jobject) buf_global_ref, jdata);
    env->DeleteLocalRef(jdata);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

extern "C" bool litert_bridge_kotlin_read_bool(void *buf_global_ref, bool **out_data,
                                                size_t *out_len, char **out_err) {
    JNIEnv *env = current_env();
    auto jresult = (jbooleanArray) env->CallStaticObjectMethod(g_shim_class, g_mid_read_bool,
                                                                (jobject) buf_global_ref);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    jsize len = env->GetArrayLength(jresult);
    auto *jdata_tmp = (jboolean *) malloc(sizeof(jboolean) * (size_t) len);
    env->GetBooleanArrayRegion(jresult, 0, len, jdata_tmp);
    env->DeleteLocalRef(jresult);
    auto *data = (bool *) malloc(sizeof(bool) * (size_t) len);
    for (jsize i = 0; i < len; i++) {
        data[i] = jdata_tmp[i] != JNI_FALSE;
    }
    free(jdata_tmp);
    *out_data = data;
    *out_len = (size_t) len;
    return true;
}

extern "C" bool litert_bridge_kotlin_write_long(void *buf_global_ref, const int64_t *data,
                                                 size_t num_elements, char **out_err) {
    JNIEnv *env = current_env();
    jlongArray jdata = env->NewLongArray((jsize) num_elements);
    env->SetLongArrayRegion(jdata, 0, (jsize) num_elements, (const jlong *) data);
    env->CallStaticVoidMethod(g_shim_class, g_mid_write_long, (jobject) buf_global_ref, jdata);
    env->DeleteLocalRef(jdata);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

extern "C" bool litert_bridge_kotlin_read_long(void *buf_global_ref, int64_t **out_data,
                                                size_t *out_len, char **out_err) {
    JNIEnv *env = current_env();
    auto jresult = (jlongArray) env->CallStaticObjectMethod(g_shim_class, g_mid_read_long,
                                                             (jobject) buf_global_ref);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    jsize len = env->GetArrayLength(jresult);
    auto *data = (int64_t *) malloc((size_t) len * sizeof(int64_t));
    env->GetLongArrayRegion(jresult, 0, len, (jlong *) data);
    env->DeleteLocalRef(jresult);
    *out_data = data;
    *out_len = (size_t) len;
    return true;
}

extern "C" bool litert_bridge_kotlin_run(void *model_global_ref, void *const *input_global_refs,
                                          size_t num_inputs, void *const *output_global_refs,
                                          size_t num_outputs, char **out_err) {
    JNIEnv *env = current_env();
    jclass tb_class = env->FindClass("com/google/ai/edge/litert/TensorBuffer");
    jobjectArray jinputs = env->NewObjectArray((jsize) num_inputs, tb_class, nullptr);
    for (size_t i = 0; i < num_inputs; i++) {
        env->SetObjectArrayElement(jinputs, (jsize) i, (jobject) input_global_refs[i]);
    }
    jobjectArray joutputs = env->NewObjectArray((jsize) num_outputs, tb_class, nullptr);
    for (size_t i = 0; i < num_outputs; i++) {
        env->SetObjectArrayElement(joutputs, (jsize) i, (jobject) output_global_refs[i]);
    }
    env->DeleteLocalRef(tb_class);

    env->CallStaticVoidMethod(g_shim_class, g_mid_run, (jobject) model_global_ref, jinputs, joutputs);
    env->DeleteLocalRef(jinputs);
    env->DeleteLocalRef(joutputs);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

// ---- close(), always through the shim. ----

namespace {

bool close_impl(jmethodID mid, void *global_ref, char **out_err) {
    JNIEnv *env = current_env();
    env->CallStaticVoidMethod(g_shim_class, mid, (jobject) global_ref);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    env->DeleteGlobalRef((jobject) global_ref);
    return true;
}

}  // namespace

extern "C" bool litert_bridge_close_environment(void *global_ref, char **out_err) {
    return close_impl(g_mid_close_environment, global_ref, out_err);
}

extern "C" bool litert_bridge_close_compiled_model(void *global_ref, char **out_err) {
    return close_impl(g_mid_close_compiled_model, global_ref, out_err);
}

extern "C" bool litert_bridge_close_tensor_buffer(void *global_ref, char **out_err) {
    return close_impl(g_mid_close_tensor_buffer, global_ref, out_err);
}
