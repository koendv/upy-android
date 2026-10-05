// JNI-facing bridge for the camera module (camera_module.cpp), calling
// CameraShim.kt. Plain C types only, so camera_module.cpp never sees
// <jni.h> (qstr scan, see mqtt_jni_bridge.h).
#ifndef UPY_ANDROID_CAMERA_JNI_BRIDGE_H
#define UPY_ANDROID_CAMERA_JNI_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// camera_bridge_open() statuses, same values as CameraShim.OK etc.
#define CAMERA_BRIDGE_OK 0
#define CAMERA_BRIDGE_NO_PERMISSION 1
#define CAMERA_BRIDGE_NO_CAMERA 2

// Ring slots, same as CameraShim.SLOTS.
#define CAMERA_BRIDGE_SLOTS 3

typedef struct {
    int slot;
    int64_t seq;
    int64_t timestamp_ns;
    int32_t width;   // sensor orientation
    int32_t height;
    int32_t rotation_degrees;
} camera_frame_t;

void camera_bridge_init_impl(void *jni_env);

// Every bool function: true on success; on failure false and *out_err
// is a malloc'd message the caller frees.

// camera.list(): count() refreshes the list, the others index into it.
// Strings and sizes are malloc'd, the caller frees them.
bool camera_bridge_count(int *out_count, char **out_err);
bool camera_bridge_id(int index, char **out_id, char **out_err);
bool camera_bridge_facing(int index, char **out_facing, char **out_err);
// *out_sizes: n_sizes (w, h) pairs.
bool camera_bridge_sizes(int index, int32_t **out_sizes, size_t *out_n_sizes, char **out_err);

// id null: default camera. *out_status: CAMERA_BRIDGE_*.
bool camera_bridge_open(const char *id, int width, int height, int *out_status, char **out_err);

// out: out_w, out_h, src_w, src_h, rotation_degrees.
bool camera_bridge_open_info(int32_t out[5], char **out_err);

// Address of ring slot `index` (a direct ByteBuffer), valid until close.
bool camera_bridge_buffer(int index, uint8_t **out_data, size_t *out_size, char **out_err);

// Waits up to timeout_ms for a frame newer than last_seq. On success,
// *out_has_frame false means timeout or interrupt; true means *out is
// filled in and the slot is held until camera_bridge_release(). An
// error means the camera is lost.
bool camera_bridge_acquire(int64_t last_seq, long timeout_ms, bool *out_has_frame,
                           camera_frame_t *out, char **out_err);

void camera_bridge_release(void);

bool camera_bridge_close(char **out_err);

// Wakes a waiting camera_bridge_acquire(). Any JVM-attached thread.
void camera_bridge_interrupt(void);

#ifdef __cplusplus
}
#endif

#endif
