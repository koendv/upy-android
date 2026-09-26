// JNI-facing bridge for litert (litert_module.cpp). Deliberately NOT
// included by litert_module.cpp with <jni.h> types exposed --
// litert_module.cpp is qstr-scanned (SRC_QSTR in micropython_embed.mk)
// and there is no qstr-stub for <jni.h>, the same reason engine_jni.cpp
// is excluded from that list. Every function here uses only primitive
// C types (void* for JNI object refs, long for raw LiteRt C-API
// handles) so litert_module.cpp never needs to see a real
// jobject/JNIEnv*. litert_jni_bridge.cpp is the one file that
// #includes <jni.h> and calls into LiteRtShim.kt.
#ifndef UPY_ANDROID_LITERT_JNI_BRIDGE_H
#define UPY_ANDROID_LITERT_JNI_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called once from litert_module.h's litert_bridge_init (same function,
// declared there since engine_jni.cpp -- which has a real JNIEnv* --
// calls it, not litert_module.cpp).
void litert_bridge_init_impl(void *jni_env);

// Every function below: returns true on success. On failure, returns
// false and sets *out_err to a malloc'd (strdup'd) error string the
// caller (litert_module.cpp) must free() after using it to
// raise_os_error -- matches this project's own raise_os_error_status
// pattern (a status-int variant used elsewhere in this codebase), just
// plumbed through a malloc'd string instead of a LiteRtStatus int,
// since JNI exceptions don't have one.
// On success, *out_err is left untouched (caller must init it to
// nullptr and only read a real message on failure).

bool litert_bridge_create_environment(long *out_handle, void **out_global_ref, char **out_err);

bool litert_bridge_create_compiled_model(void *env_global_ref, const char *path,
                                          int accelerator_value, long *out_handle,
                                          void **out_global_ref, char **out_err);

// *out_count buffers created; *out_handles/*out_global_refs are malloc'd
// arrays of that length, owned by the caller (free()'d by
// litert_module.cpp once copied into its own registry). v0 does NOT
// also query each buffer's byte size here -- see
// litert_bridge_kotlin_read_int8's own comment below for why.
bool litert_bridge_create_input_buffers(void *model_global_ref, long **out_handles,
                                         void ***out_global_refs,
                                         size_t *out_count, char **out_err);
bool litert_bridge_create_output_buffers(void *model_global_ref, long **out_handles,
                                          void ***out_global_refs,
                                          size_t *out_count, char **out_err);

// One JNI call each, through LiteRtShim.
bool litert_bridge_kotlin_write_int8(void *buf_global_ref, const int8_t *data, size_t len,
                                      char **out_err);
// *out_data is a malloc'd buffer of *out_len bytes, owned by the caller
// (litert_module.cpp frees it after copying into a MicroPython bytes
// object). Length comes from the real TensorBuffer.readInt8() jbyteArray
// (GetArrayLength), not from a value cached at buffer-creation time --
// this build deliberately never tracks a buffer's byte size up front,
// since the only way found to learn it (LiteRtGetTensorBufferPackedSize
// on the extracted raw handle) returned inconsistent garbage during v0
// development despite the handle itself being verified genuine.
bool litert_bridge_kotlin_read_int8(void *buf_global_ref, int8_t **out_data, size_t *out_len,
                                     char **out_err);

// write_float/read_float, write_int/read_int, write_bool/read_bool,
// write_long/read_long: real typed TensorBuffer methods (writeFloat/
// readFloat/writeInt/readInt/writeBoolean/readBoolean/writeLong/
// readLong, confirmed via javap against the real litert-api jar), not
// a byte-reinterpret through the int8 pair above. Every *_count/*out_len
// below is an ELEMENT count, not a byte length. Same GetArrayLength()-
// derived-length reasoning as litert_bridge_kotlin_read_int8 above for
// every read function; every *out_data is a malloc'd array owned by
// the caller.
bool litert_bridge_kotlin_write_float(void *buf_global_ref, const float *data,
                                       size_t num_elements, char **out_err);
bool litert_bridge_kotlin_read_float(void *buf_global_ref, float **out_data, size_t *out_len,
                                      char **out_err);

bool litert_bridge_kotlin_write_int(void *buf_global_ref, const int32_t *data,
                                     size_t num_elements, char **out_err);
bool litert_bridge_kotlin_read_int(void *buf_global_ref, int32_t **out_data, size_t *out_len,
                                    char **out_err);

bool litert_bridge_kotlin_write_bool(void *buf_global_ref, const bool *data,
                                      size_t num_elements, char **out_err);
bool litert_bridge_kotlin_read_bool(void *buf_global_ref, bool **out_data, size_t *out_len,
                                     char **out_err);

bool litert_bridge_kotlin_write_long(void *buf_global_ref, const int64_t *data,
                                      size_t num_elements, char **out_err);
bool litert_bridge_kotlin_read_long(void *buf_global_ref, int64_t **out_data, size_t *out_len,
                                     char **out_err);

bool litert_bridge_kotlin_run(void *model_global_ref, void *const *input_global_refs,
                               size_t num_inputs, void *const *output_global_refs,
                               size_t num_outputs, char **out_err);

// close() always goes through the shim (Kotlin's own AutoCloseable).
// Also releases the JNI global ref (litert_bridge_delete_global_ref) on
// success; the caller doesn't need a separate call for that.
bool litert_bridge_close_environment(void *global_ref, char **out_err);
bool litert_bridge_close_compiled_model(void *global_ref, char **out_err);
bool litert_bridge_close_tensor_buffer(void *global_ref, char **out_err);

#ifdef __cplusplus
}
#endif

#endif
