// upy-android native rt module. OUR OWN code, NOT vendored OpenMV
// source.
#ifndef UPY_ANDROID_RT_MODULE_H
#define UPY_ANDROID_RT_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Idempotent, safe to call when nothing is open. Same tier/contract as
// camera_close_all()/imu_close_all()/tf_close_all(), called from the
// same two places in engine_jni.cpp, BEFORE mp_embed_deinit(). Destroys
// every registered Model's native LiteRT handles first, then the shared
// LiteRtEnvironment last, then resets it to null so a fresh one is
// created lazily the next time a script calls Model().
// see session-state: rt_module.cpp#registry_design
void rt_close_all(void);

#ifdef __cplusplus
}
#endif

#endif
