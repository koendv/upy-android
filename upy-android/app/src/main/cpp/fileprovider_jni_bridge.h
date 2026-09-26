// JNI-facing bridge for android.fileprovider (fileprovider_module.cpp).
// Deliberately NOT included by fileprovider_module.cpp with <jni.h>
// types exposed -- same qstr-scanning reasoning as
// mediastore_jni_bridge.h/litert_jni_bridge.h. Every function here uses
// only primitive C types so fileprovider_module.cpp never needs to see
// a real jobject/JNIEnv*.
//
// Unlike mediastore, this bridge never touches a real Context/
// ContentResolver -- it only calls a static Kotlin method
// (EngineService.requestShare) that hands the request across process
// boundaries via the IEngineShareListener AIDL callback. No global ref
// to a Context is needed here at all.
#ifndef UPY_ANDROID_FILEPROVIDER_JNI_BRIDGE_H
#define UPY_ANDROID_FILEPROVIDER_JNI_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// Called once from fileprovider_module.cpp's own
// fileprovider_bridge_init() wrapper (fileprovider_module.h -- same
// indirection litert_module.h/mediastore_module.h use). Resolves and
// caches EngineService's class/static-method IDs.
void fileprovider_bridge_init_impl(void *jni_env);

// path/mime_type: passed straight through to
// EngineService.requestShare(), which forwards them (best-effort, via
// the oneway IEngineShareListener AIDL callback) into the main process
// for the actual FileProvider URI build + ACTION_SEND share sheet. This
// call itself cannot fail in a way a script needs to observe -- a
// missing/dead listener in the main process is dropped silently there
// (see EngineService.kt's own requestShare(), matching setOutputListener's
// RemoteException handling) -- so this only returns false/sets *out_err
// for a genuine local JNI failure (e.g. the static method could not be
// resolved), never for "nothing was listening".
bool fileprovider_bridge_share(const char *path, const char *mime_type, char **out_err);

#ifdef __cplusplus
}
#endif

#endif
