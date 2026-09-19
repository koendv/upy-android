// upy-android native tf module. OUR OWN code, NOT vendored OpenMV
// source.
#ifndef UPY_ANDROID_TF_MODULE_H
#define UPY_ANDROID_TF_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Idempotent, safe to call when nothing is open. Same tier/contract as
// camera_close_all()/imu_close_all(), called from the same two places
// in engine_jni.cpp, BEFORE mp_embed_deinit(). Unlike those two,
// android.tf.Model is genuinely multi-instance (no single shared
// g_cam/g_imu-style global), so this walks a registry of every
// still-open Model's native LiteRT handles and tears each down
// directly.
// see session-state: tf_module.cpp#registry_design
void tf_close_all(void);

#ifdef __cplusplus
}
#endif

#endif
