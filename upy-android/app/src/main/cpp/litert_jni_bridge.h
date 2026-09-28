// JNI-facing bridge for litert (litert_module.cpp). Deliberately not
// included by litert_module.cpp with <jni.h> types exposed.
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

// Called once from litert_module.h's litert_bridge_init (same
// function, declared there since engine_jni.cpp, which has a real
// JNIEnv*, calls it, not litert_module.cpp).
void litert_bridge_init_impl(void *jni_env);

// see session-state: litert_jni_bridge.h#litert_bridge_kotlin_read_int8

bool litert_bridge_create_environment(long *out_handle, void **out_global_ref, char **out_err);

bool litert_bridge_create_compiled_model(void *env_global_ref, const char *path,
                                          int accelerator_value, long *out_handle,
                                          void **out_global_ref, char **out_err);

// see session-state: litert_jni_bridge.h#litert_bridge_kotlin_read_int8
bool litert_bridge_create_input_buffers(void *model_global_ref, long **out_handles,
                                         void ***out_global_refs,
                                         size_t *out_count, char **out_err);
bool litert_bridge_create_output_buffers(void *model_global_ref, long **out_handles,
                                          void ***out_global_refs,
                                          size_t *out_count, char **out_err);

// One JNI call each, through LiteRtShim.
bool litert_bridge_kotlin_write_int8(void *buf_global_ref, const int8_t *data, size_t len,
                                      char **out_err);
// see session-state: litert_jni_bridge.h#litert_bridge_kotlin_read_int8
bool litert_bridge_kotlin_read_int8(void *buf_global_ref, int8_t **out_data, size_t *out_len,
                                     char **out_err);

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

// see session-state: litert_jni_bridge.h#litert_bridge_kotlin_read_int8
bool litert_bridge_close_environment(void *global_ref, char **out_err);
bool litert_bridge_close_compiled_model(void *global_ref, char **out_err);
bool litert_bridge_close_tensor_buffer(void *global_ref, char **out_err);

#ifdef __cplusplus
}
#endif

#endif
