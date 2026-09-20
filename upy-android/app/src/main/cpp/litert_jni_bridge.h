// JNI-facing bridge for android.litert. Deliberately NOT included by
// litert_module.cpp with <jni.h> types exposed -- litert_module.cpp is
// qstr-scanned (SRC_QSTR in micropython_embed.mk) and there is no
// qstr-stub for <jni.h>, the same reason engine_jni.cpp is excluded
// from that list. Every function here uses only primitive C types
// (void* for JNI object refs, long for raw LiteRt C-API handles) so
// litert_module.cpp never needs to see a real jobject/JNIEnv*.
// litert_jni_bridge.cpp is the one file that #includes <jni.h> AND
// calls libLiteRt.so's C API directly (both the JNI-calling 'kotlin'
// backend and the direct-call 'c' backend live there, see
// litert_module.cpp#module_design for why both belong in the same
// file rather than split further).
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
// pattern (rt_module.cpp), just plumbed through a malloc'd string
// instead of a LiteRtStatus int, since JNI exceptions don't have one.
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

// 'c' backend: direct libLiteRt.so calls on the raw handle, zero JNI.
// NOT YET ENABLED in v0 -- see this pair's own definitions in
// litert_jni_bridge.cpp for why (a real, unresolved bug in querying a
// Kotlin-created buffer's byte size via the C API, found during v0
// development). Present and believed correct in isolation (same
// lock/memcpy/unlock idiom rt_module.cpp already proves out), just not
// reachable yet -- android.litert.set_backend('c') raises clearly in
// this build instead of reaching these.
bool litert_bridge_c_write_int8(long buf_handle, const int8_t *data, size_t len, char **out_err);
bool litert_bridge_c_read_int8(long buf_handle, int8_t *out_data, size_t len, char **out_err);
bool litert_bridge_c_run(long model_handle, const long *input_handles, size_t num_inputs,
                          const long *output_handles, size_t num_outputs, char **out_err);

// 'kotlin' backend: one JNI call each, through LiteRtShim. The only
// backend v0 actually uses.
bool litert_bridge_kotlin_write_int8(void *buf_global_ref, const int8_t *data, size_t len,
                                      char **out_err);
// *out_data is a malloc'd buffer of *out_len bytes, owned by the caller
// (litert_module.cpp frees it after copying into a MicroPython bytes
// object). Length comes from the real TensorBuffer.readInt8() jbyteArray
// (GetArrayLength), not from a value cached at buffer-creation time --
// v0 deliberately never tracks a buffer's byte size up front, since the
// only way found to learn it (LiteRtGetTensorBufferPackedSize on the
// extracted raw handle) returned inconsistent garbage during
// development despite the handle itself being verified genuine.
bool litert_bridge_kotlin_read_int8(void *buf_global_ref, int8_t **out_data, size_t *out_len,
                                     char **out_err);
bool litert_bridge_kotlin_run(void *model_global_ref, void *const *input_global_refs,
                               size_t num_inputs, void *const *output_global_refs,
                               size_t num_outputs, char **out_err);

// close() always goes through the shim (Kotlin's own AutoCloseable),
// regardless of which hot-path backend was active -- see
// litert_module.cpp#module_design. Also releases the JNI global ref
// (litert_bridge_delete_global_ref) on success; the caller doesn't need
// a separate call for that.
bool litert_bridge_close_environment(void *global_ref, char **out_err);
bool litert_bridge_close_compiled_model(void *global_ref, char **out_err);
bool litert_bridge_close_tensor_buffer(void *global_ref, char **out_err);

#ifdef __cplusplus
}
#endif

#endif
