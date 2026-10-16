// JNI-facing bridge for http (http_module.cpp). Only primitive C types here,
// so http_module.cpp never sees a jobject/JNIEnv*, same reason as
// mqtt_jni_bridge.h. http_jni_bridge.cpp is the one file that #includes
// <jni.h> and calls HttpShim.kt.
//
// Every call blocks the calling (interpreter) thread. A function returns true
// on success. On failure it returns false and sets *out_err to a malloc'd
// string the caller must free().
#ifndef UPY_ANDROID_HTTP_JNI_BRIDGE_H
#define UPY_ANDROID_HTTP_JNI_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void http_bridge_init_impl(void *jni_env);

// Interpreter thread, before it checks for a pending Interrupt and calls
// get, post or download.
void http_bridge_begin(void);

// Any thread (Interrupt). Ends the running call, or the next one before it
// starts to wait.
void http_bridge_cancel(void);

// *out_data: malloc'd body of *out_len bytes; the caller frees it.
bool http_bridge_get(const char *url, double timeout_s, long long max_bytes,
                     uint8_t **out_data, size_t *out_len, char **out_err);

bool http_bridge_post(const char *url, const uint8_t *data, size_t len, const char *content_type,
                      double timeout_s, long long max_bytes,
                      uint8_t **out_data, size_t *out_len, char **out_err);

bool http_bridge_download(const char *url, const char *path, bool progress, double timeout_s, char **out_err);

#ifdef __cplusplus
}
#endif

#endif
