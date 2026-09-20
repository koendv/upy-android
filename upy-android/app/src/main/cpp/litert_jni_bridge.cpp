// JNI-facing implementation for android.litert. Both hot-path backends
// live here: 'kotlin' (one JNI call each, through LiteRtShim) and 'c'
// (direct libLiteRt.so calls on a raw handle extracted via JNI field
// access, zero further JNI once the handle is in hand) -- see
// litert_jni_bridge.h's own header comment for why litert_module.cpp
// never sees a real jobject/JNIEnv*.
// see session-state: litert_module.cpp#hot_path_mechanism

#include "litert_jni_bridge.h"

#include <jni.h>

#include <cstdlib>
#include <cstring>

#include "litert/c/litert_common.h"
#include "litert/c/litert_compiled_model.h"
#include "litert/c/litert_tensor_buffer.h"

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
jmethodID g_mid_run = nullptr;
jmethodID g_mid_close_environment = nullptr;
jmethodID g_mid_close_compiled_model = nullptr;
jmethodID g_mid_close_tensor_buffer = nullptr;

// Only ever called on the single persistent "mp-engine-worker" thread
// (see EngineWorker.kt / engine_jni.cpp#threading_contract), which is a
// real java.lang.Thread from birth and stays JVM-attached for the
// process's entire lifetime -- so GetEnv() here is a plain lookup, not
// an attach, and never returns JNI_EDETACHED in practice. No
// AttachCurrentThread()/DetachCurrentThread() anywhere in this file.
JNIEnv *current_env() {
    JNIEnv *env = nullptr;
    g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    return env;
}

// Applied after EVERY Call*Method/CallStatic*Method in this file, no
// exceptions. ExceptionClear() must happen before any further JNI call
// and before litert_module.cpp's raise_os_error() (which longjmps via
// nlr_raise()) -- a pending Java exception surviving a longjmp corrupts
// JVM state.
char *describe_and_clear_exception(JNIEnv *env) {
    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!exc) {
        return strdup("android.litert: unknown error");
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
// works across that class's subclasses -- no per-instance FindClass
// needed. GetFieldID/GetLongField operate on the compiled classfile
// directly and are blind to Kotlin's compiler-level `internal`
// visibility check (which is why this extraction lives here, in C++,
// and not in LiteRtShim.kt -- see that file's own header comment for
// the empirical proof).
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
    // Any null jfieldID/jmethodID above is a packaging/signature bug --
    // caught at first use (a pending NoSuchFieldError/NoSuchMethodError
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

// v0: no packed-size query here at all (not just deferred -- see this
// file's own header comment on litert_bridge_kotlin_read_int8 for why
// buffer byte-size isn't tracked up front in this build).
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

// ---- 'c' backend: direct libLiteRt.so calls, zero JNI. NOT YET
// ENABLED in v0 -- android.litert.set_backend('c') raises clearly
// rather than reaching this code (see litert_module.cpp). The
// lock/memcpy/unlock idiom itself mirrors rt_module.cpp's own proven
// set_input_ndarray()/get_output_ndarray() and should be sound; what's
// NOT yet resolved is how to learn a Kotlin-created TensorBuffer's real
// byte size without going through Kotlin -- LiteRtGetTensorBufferPackedSize()
// on a handle extracted this way returned inconsistent garbage across
// buffers in the same batch during v0 development (large garbage / 0 /
// different garbage for buffers created in one call), while the
// extracted handle itself was confirmed to be a genuine, valid pointer
// (not corrupted) via a temporary diagnostic. Needs its own focused
// investigation before enabling -- see session-state. ----

extern "C" bool litert_bridge_c_write_int8(long buf_handle, const int8_t *data, size_t len,
                                            char **out_err) {
    auto buf = (LiteRtTensorBuffer) (void *) buf_handle;
    void *host_ptr = nullptr;
    if (LiteRtLockTensorBuffer(buf, &host_ptr, kLiteRtTensorBufferLockModeWrite) != kLiteRtStatusOk) {
        *out_err = strdup("android.litert: failed to lock tensor buffer for write");
        return false;
    }
    memcpy(host_ptr, data, len);
    LiteRtUnlockTensorBuffer(buf);
    return true;
}

extern "C" bool litert_bridge_c_read_int8(long buf_handle, int8_t *out_data, size_t len,
                                           char **out_err) {
    auto buf = (LiteRtTensorBuffer) (void *) buf_handle;
    void *host_ptr = nullptr;
    if (LiteRtLockTensorBuffer(buf, &host_ptr, kLiteRtTensorBufferLockModeRead) != kLiteRtStatusOk) {
        *out_err = strdup("android.litert: failed to lock tensor buffer for read");
        return false;
    }
    memcpy(out_data, host_ptr, len);
    LiteRtUnlockTensorBuffer(buf);
    return true;
}

extern "C" bool litert_bridge_c_run(long model_handle, const long *input_handles,
                                     size_t num_inputs, const long *output_handles,
                                     size_t num_outputs, char **out_err) {
    auto *input_bufs = (LiteRtTensorBuffer *) malloc(sizeof(LiteRtTensorBuffer) * num_inputs);
    auto *output_bufs = (LiteRtTensorBuffer *) malloc(sizeof(LiteRtTensorBuffer) * num_outputs);
    for (size_t i = 0; i < num_inputs; i++) {
        input_bufs[i] = (LiteRtTensorBuffer) (void *) input_handles[i];
    }
    for (size_t i = 0; i < num_outputs; i++) {
        output_bufs[i] = (LiteRtTensorBuffer) (void *) output_handles[i];
    }
    auto model = (LiteRtCompiledModel) (void *) model_handle;
    LiteRtStatus status = LiteRtRunCompiledModel(model, /*signature_index=*/0,
                                                  num_inputs, input_bufs,
                                                  num_outputs, output_bufs);
    free(input_bufs);
    free(output_bufs);
    if (status != kLiteRtStatusOk) {
        char buf[96];
        snprintf(buf, sizeof(buf), "android.litert: run failed (LiteRtStatus=%d)", (int) status);
        *out_err = strdup(buf);
        return false;
    }
    return true;
}

// ---- 'kotlin' backend: one JNI call each, through LiteRtShim. ----

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

// v0 reads TensorBuffer.readInt8()'s real length directly from the
// jbyteArray it returns (GetArrayLength), rather than requiring a
// pre-known size -- see this file's own note on litert_bridge_c_write_int8
// for why a C-API size query isn't used for that instead. *out_data is
// malloc'd here; the caller (litert_module.cpp) frees it.
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

// ---- close(), always through the shim regardless of backend. ----

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
