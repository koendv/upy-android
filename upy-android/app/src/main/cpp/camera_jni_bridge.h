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
#define CAMERA_BRIDGE_NO_FRAME_RATE 3

// Control and info error kinds, same values as CameraShim.ERR_*.
#define CAMERA_BRIDGE_ERR_VALUE 1
#define CAMERA_BRIDGE_ERR_UNSUPPORTED 2
#define CAMERA_BRIDGE_ERR_IO 3
#define CAMERA_BRIDGE_ERR_LOST 4

// Controls, same values as CameraShim.OP_*.
#define CAMERA_BRIDGE_OP_TORCH 0
#define CAMERA_BRIDGE_OP_TORCH_STRENGTH 1
#define CAMERA_BRIDGE_OP_LOW_LIGHT 2
#define CAMERA_BRIDGE_OP_ZOOM_RATIO 3
#define CAMERA_BRIDGE_OP_LINEAR_ZOOM 4
#define CAMERA_BRIDGE_OP_EXPOSURE 5
#define CAMERA_BRIDGE_OP_FOCUS 6
#define CAMERA_BRIDGE_OP_CANCEL_FOCUS 7

// Focus flags, same values as FocusMeteringAction.FLAG_*.
#define CAMERA_BRIDGE_FOCUS_AF 1
#define CAMERA_BRIDGE_FOCUS_AE 2
#define CAMERA_BRIDGE_FOCUS_AWB 4

// CameraInfo keys, same values as CameraShim.INFO_*.
#define CAMERA_BRIDGE_INFO_SENSOR_ROTATION 0
#define CAMERA_BRIDGE_INFO_INTRINSIC_ZOOM 1
#define CAMERA_BRIDGE_INFO_HAS_FLASH 2
#define CAMERA_BRIDGE_INFO_TORCH_STATE 3
#define CAMERA_BRIDGE_INFO_TORCH_STRENGTH_SUPPORTED 4
#define CAMERA_BRIDGE_INFO_MAX_TORCH_STRENGTH 5
#define CAMERA_BRIDGE_INFO_TORCH_STRENGTH 6
#define CAMERA_BRIDGE_INFO_ZOOM_STATE 7
#define CAMERA_BRIDGE_INFO_EXPOSURE_STATE 8
#define CAMERA_BRIDGE_INFO_FOCUS_SUPPORTED 9
#define CAMERA_BRIDGE_INFO_FRAME_RATE_RANGES 10
#define CAMERA_BRIDGE_INFO_LOW_LIGHT_SUPPORTED 11
#define CAMERA_BRIDGE_INFO_LOGICAL_MULTI_CAMERA 12
#define CAMERA_BRIDGE_INFO_LENS_FACING 20
#define CAMERA_BRIDGE_INFO_IMPLEMENTATION_TYPE 21
#define CAMERA_BRIDGE_INFO_LOW_LIGHT_STATE 22

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

// id null: default camera. rgb: RGBA frames, else grayscale (Y).
// fps_min 0: automatic frame rate. *out_status: CAMERA_BRIDGE_*.
bool camera_bridge_open(const char *id, int width, int height, bool rgb, int fps_min, int fps_max,
                        int *out_status, char **out_err);

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

// CameraControl: start one operation, then poll it. *out_kind 0 or
// CAMERA_BRIDGE_ERR_*; on an error kind, *out_err is set as above.
bool camera_bridge_control_start(int op, float a, float b, int n, int flags, int *out_kind, char **out_err);
// *out_done false: still running. Done: *out_kind, and *out_value
// (exposure index, focus success 1/0, else 0).
bool camera_bridge_control_wait(long timeout_ms, bool *out_done, int *out_kind, double *out_value, char **out_err);
void camera_bridge_control_abandon(void);

// CameraInfo of the open camera. *out_open false: no camera open.
// *out_values is malloc'd, *out_n doubles.
bool camera_bridge_info_numbers(int key, float x, float y, bool *out_open, double **out_values, size_t *out_n,
                                char **out_err);
bool camera_bridge_info_string(int key, bool *out_open, char **out_value, char **out_err);

// Wakes a waiting camera_bridge_acquire(). Any JVM-attached thread.
void camera_bridge_interrupt(void);

#ifdef __cplusplus
}
#endif

#endif
