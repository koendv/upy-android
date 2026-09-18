// upy-android native tf module -- OUR OWN code, NOT vendored OpenMV
// source. Lives here in app/src/main/cpp/ alongside camera_module.cpp/
// imu_module.cpp, same reasoning as those. See tf_module.cpp's own
// header comment for the android.tf.Model design (multi-instance,
// LiteRT C API, native-only close-all registry).
#ifndef UPY_ANDROID_TF_MODULE_H
#define UPY_ANDROID_TF_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Idempotent, safe to call when nothing is open -- same tier/contract as
// camera_close_all()/imu_close_all() (see their own header comments),
// called from the same two places in engine_jni.cpp, BEFORE
// mp_embed_deinit(). Unlike those two, android.tf.Model is genuinely
// multi-instance (no single shared g_cam/g_imu-style global), so this
// walks a registry of every still-open Model's native LiteRT handles and
// tears each down directly -- see tf_module.cpp's own comment on why
// that registry deliberately holds ONLY native pointers, never an
// mp_obj_t, and is therefore safe to call unconditionally here even
// though it runs before the GC heap those objects live on is discarded.
void tf_close_all(void);

#ifdef __cplusplus
}
#endif

#endif
