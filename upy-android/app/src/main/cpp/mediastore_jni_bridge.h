// JNI-facing bridge for android.mediastore (mediastore_module.cpp).
// Deliberately not included by mediastore_module.cpp with <jni.h>
// types exposed. mediastore_module.cpp is qstr-scanned (SRC_QSTR in
// micropython_embed.mk) and there is no qstr-stub for <jni.h>, same
// reasoning as litert_jni_bridge.h. Every function here uses only
// primitive C types (void* for JNI object refs) so mediastore_module.cpp
// never needs to see a real jobject/JNIEnv*. mediastore_jni_bridge.cpp
// is the one file that #includes <jni.h> and calls into
// MediaStoreShim.kt.
#ifndef UPY_ANDROID_MEDIASTORE_JNI_BRIDGE_H
#define UPY_ANDROID_MEDIASTORE_JNI_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called once from mediastore_module.cpp's own mediastore_bridge_init()
// wrapper (mediastore_module.h, same indirection litert_module.h/
// litert_jni_bridge.h use, so engine_jni.cpp only ever needs to
// include mediastore_module.h). Resolves and caches MediaStoreShim's
// class/method, and holds a global ref to the application Context
// (application_context is a JNIEnv-local jobject at call time; this
// function promotes it to a global ref itself, the caller does not
// need to). Unlike litert's own init, this one genuinely needs a real
// Context (MediaStore access goes through ContentResolver, which only
// a Context provides). See engine_jni.cpp's own nativeInit for where
// application_context comes from.
void mediastore_bridge_init_impl(void *jni_env, void *application_context);

// data/len: raw bytes to save (a script's own encoded image, e.g.
// img.compress().bytearray(), this module doesn't know or care about
// pixel formats, same "just bytes" contract as file I/O). display_name/
// mime_type: passed straight through to MediaStore.Images.Media's own
// ContentValues. On success, *out_uri is a malloc'd (strdup'd) string
// (the real content:// URI, owned by the caller, who must free() it)
// and the function returns true. On failure, returns false and sets
// *out_err instead (also malloc'd/owned by the caller), same contract
// as litert_jni_bridge.h's own functions.
bool mediastore_bridge_save_image(const uint8_t *data, size_t len,
                                   const char *display_name, const char *mime_type,
                                   char **out_uri, char **out_err);

#ifdef __cplusplus
}
#endif

#endif
