// JNI-facing bridge for zipfile (zipfile_module.cpp). Only primitive C
// types here, so zipfile_module.cpp never sees a jobject/JNIEnv*, same
// reason as mqtt_jni_bridge.h. zipfile_jni_bridge.cpp is the one file that
// #includes <jni.h> and calls ZipShim.kt.
//
// Every function returns true on success. On failure it returns false and
// sets *out_err to a malloc'd string the caller must free().
#ifndef UPY_ANDROID_ZIPFILE_JNI_BRIDGE_H
#define UPY_ANDROID_ZIPFILE_JNI_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void zipfile_bridge_init_impl(void *jni_env);

// *out_entries: malloc'd array of *out_count malloc'd strings "<size> <name>".
// The caller frees each string and the array.
bool zipfile_bridge_info(const char *zip, char ***out_entries, size_t *out_count, char **out_err);

bool zipfile_bridge_read(const char *zip, const char *dest, char **out_err);

bool zipfile_bridge_write(const char *zip, const char *path, char **out_err);

#ifdef __cplusplus
}
#endif

#endif
