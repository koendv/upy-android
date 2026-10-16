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

// Request options. headers: header_count names and values in turn, so
// 2 * header_count strings. user and password: Basic authentication, both null
// for none.
typedef struct {
    const char *const *headers;
    size_t header_count;
    const char *user;
    const char *password;
} http_options_t;

// *out_data: malloc'd body of *out_len bytes; the caller frees it.
bool http_bridge_get(const char *url, double timeout_s, long long max_bytes, const http_options_t *options,
                     uint8_t **out_data, size_t *out_len, char **out_err);

// method: "POST" or "PUT". default_type: the Content-Type unless a header gives one.
bool http_bridge_send(const char *method, const char *url, const uint8_t *data, size_t len, const char *default_type,
                      double timeout_s, long long max_bytes, const http_options_t *options,
                      uint8_t **out_data, size_t *out_len, char **out_err);

bool http_bridge_download(const char *url, const char *path, bool progress, double timeout_s,
                          const http_options_t *options, char **out_err);

#ifdef __cplusplus
}
#endif

#endif
