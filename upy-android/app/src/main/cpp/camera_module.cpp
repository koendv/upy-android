// upy-android native csi (camera) module -- OUR OWN bridge between
// py_image.c's Image type and Android's NDK Camera2 API (libcamera2ndk +
// libmediandk), NOT vendored OpenMV source. See camera_module.h and
// SESSION_STATE.yaml's camera/display scoping discussion for why this
// lives here rather than under my-overrides/openmv/.
//
// Scope of this first bring-up (SESSION_STATE.yaml has the full design):
// csi.CSI(), reset(), pixformat(), framesize(), snapshot() (both the
// plain form and the time=/frames= sensor-settle warm-up form -- added
// after on-device testing confirmed the very first frame after reset()
// is genuinely under-exposed on real hardware, not just a theoretical
// gap). auto_gain()/auto_whitebal() deliberately NOT included yet:
// ACAMERA_CONTROL_AE_MODE_OFF without also setting real manual exposure/
// sensitivity metadata risks silently black/garbage frames on real
// hardware -- a half-correct implementation would be worse than the
// AttributeError a script gets by omission. display module is a
// separate, later piece (needs a real Surface, which nothing in this
// native-only bring-up has access to).
//
// Camera2/NDK-camera-location decision (SESSION_STATE.yaml): capture
// runs natively inside :engine (this file), not the Java Camera2 API in
// MainActivity -- confirmed viable via an earlier on-device probe.

#include "camera_module.h"

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCaptureRequest.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImageReader.h>
#include <media/NdkImage.h>
#include <android/log.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <vector>
#include <algorithm>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/nlr.h"
#include "py/mperrno.h"
#include "imlib.h"
#include "py_image.h"
}

#define LOG_TAG "upy-camera"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

// Same 5ms interrupt-check granularity mp_hal_delay_ms() uses
// (my-overrides/mphalport.c's MP_HAL_DELAY_CHUNK_MS) -- reused so
// interrupt responsiveness stays uniform across every blocking call in
// the app, not reinvented for this one.
constexpr long kWaitChunkMs = 5;
// Bounded automatic failure detection -- generous enough to absorb
// Camera2's well-documented first-frame 3A convergence latency (see
// SESSION_STATE.yaml's csi.snapshot() timeout-as-error decision).
constexpr long kSnapshotTimeoutMs = 500;

// Default framesize (QVGA) -- real OpenMV boards' own reset default is
// PIXFORMAT_INVALID/no framesize at all, requiring a script to set both
// explicitly before snapshot(); we default to something reasonable
// instead purely so an early snapshot() (before framesize() is called)
// still produces a real image rather than an error, matching the
// friendlier failure mode of "wrong size" over "won't run at all".
constexpr int32_t kDefaultWidth = 320;
constexpr int32_t kDefaultHeight = 240;

struct CameraState {
    ACameraManager *manager;
    ACameraDevice *device;
    ACameraCaptureSession *session;
    ACaptureSessionOutputContainer *output_container;
    ACaptureSessionOutput *session_output;
    ACameraOutputTarget *output_target;
    ACaptureRequest *request;
    AImageReader *reader;
    ANativeWindow *reader_window;
    int32_t width;
    int32_t height;
    pixformat_t pixfmt;
    // Set only while a snapshot() call is actively waiting for a frame --
    // camera_interrupt_active_wait() (called from nativeInterrupt(), any
    // thread) posts into this if non-null. Written only from the worker
    // thread except for that one post, which is what sem_post() is for.
    sem_t *active_wait_sem;
};

CameraState g_cam = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    kDefaultWidth, kDefaultHeight, PIXFORMAT_GRAYSCALE, nullptr,
};

typedef struct _csi_obj_t {
    mp_obj_base_t base;
} csi_obj_t;

// Matches mp_raise_OSError_with_filename's own shape (py/runtime.c) --
// a real two-arg OSError(errno, message), not just a bare errno number,
// so a script's `except OSError as er: er.errno == errno.EACCES` works
// (see SESSION_STATE.yaml's script-level permission-denied decision).
void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// AImageReader's onImageAvailable fires on an internal Android thread,
// not the worker thread -- the actual async producer/consumer boundary
// the semaphore-plus-nlr_push pattern in csi_snapshot() exists for.
void on_image_available(void *context, AImageReader *reader) {
    (void) reader;
    sem_post(static_cast<sem_t *>(context));
}

void on_device_disconnected(void *context, ACameraDevice *device) {
    (void) context;
    (void) device;
    LOGW("camera device disconnected");
}

void on_device_error(void *context, ACameraDevice *device, int error) {
    (void) context;
    (void) device;
    LOGW("camera device error: %d", error);
}

// Tears down the session/reader/request layer only -- the device stays
// open. Idempotent (every field checked before freeing), safe to call
// when nothing is configured yet. Used both by camera_close_all() and
// by ensure_session() to clean up before rebuilding after a
// pixformat()/framesize() change.
void close_session_and_reader() {
    if (g_cam.session) {
        ACameraCaptureSession_stopRepeating(g_cam.session);
        ACameraCaptureSession_close(g_cam.session);
        g_cam.session = nullptr;
    }
    if (g_cam.request) {
        ACaptureRequest_free(g_cam.request);
        g_cam.request = nullptr;
    }
    if (g_cam.output_target) {
        ACameraOutputTarget_free(g_cam.output_target);
        g_cam.output_target = nullptr;
    }
    if (g_cam.session_output) {
        ACaptureSessionOutput_free(g_cam.session_output);
        g_cam.session_output = nullptr;
    }
    if (g_cam.output_container) {
        ACaptureSessionOutputContainer_free(g_cam.output_container);
        g_cam.output_container = nullptr;
    }
    if (g_cam.reader) {
        // Also invalidates reader_window -- do NOT ANativeWindow_release()
        // it separately (owned by the reader, see AImageReader_getWindow's
        // own doc comment).
        AImageReader_delete(g_cam.reader);
        g_cam.reader = nullptr;
        g_cam.reader_window = nullptr;
    }
}

// (Re)builds the session/reader/request layer for the CURRENT
// width/height/pixfmt. Called lazily from csi_snapshot() rather than
// eagerly from pixformat()/framesize() -- a script may call both setters
// before ever capturing a frame, so rebuilding on every setter call
// would do wasted/duplicate work.
void ensure_session() {
    if (g_cam.session) {
        return;
    }
    close_session_and_reader();

    if (AImageReader_new(g_cam.width, g_cam.height, AIMAGE_FORMAT_YUV_420_888, 2, &g_cam.reader) != AMEDIA_OK) {
        raise_os_error(MP_EIO, "camera: failed to create image reader");
    }
    if (AImageReader_getWindow(g_cam.reader, &g_cam.reader_window) != AMEDIA_OK) {
        raise_os_error(MP_EIO, "camera: failed to get reader window");
    }
    if (ACaptureSessionOutputContainer_create(&g_cam.output_container) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "camera: failed to create output container");
    }
    if (ACaptureSessionOutput_create(g_cam.reader_window, &g_cam.session_output) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "camera: failed to create session output");
    }
    if (ACaptureSessionOutputContainer_add(g_cam.output_container, g_cam.session_output) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "camera: failed to add session output");
    }

    ACameraCaptureSession_stateCallbacks session_cb = {nullptr, nullptr, nullptr, nullptr};
    if (ACameraDevice_createCaptureSession(g_cam.device, g_cam.output_container, &session_cb, &g_cam.session) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "camera: failed to create capture session");
    }
    if (ACameraDevice_createCaptureRequest(g_cam.device, TEMPLATE_PREVIEW, &g_cam.request) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "camera: failed to create capture request");
    }
    if (ACameraOutputTarget_create(g_cam.reader_window, &g_cam.output_target) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "camera: failed to create output target");
    }
    if (ACaptureRequest_addTarget(g_cam.request, g_cam.output_target) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "camera: failed to add capture target");
    }
    if (ACameraCaptureSession_setRepeatingRequest(g_cam.session, nullptr, 1, &g_cam.request, nullptr) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "camera: failed to start capture");
    }
}

// Y plane is always full resolution, pixel stride 1 -- a strided
// row-by-row copy, no per-pixel math needed.
void convert_to_grayscale(AImage *image, image_t *out) {
    uint8_t *y_data = nullptr;
    int y_len = 0;
    int32_t y_row_stride = 0;
    AImage_getPlaneData(image, 0, &y_data, &y_len);
    AImage_getPlaneRowStride(image, 0, &y_row_stride);

    for (int32_t row = 0; row < out->h; row++) {
        memcpy(out->data + (size_t) row * out->w, y_data + (size_t) row * y_row_stride, out->w);
    }
}

// YUV_420_888 -> RGB565, real per-pixel conversion (not degraded/
// stubbed -- see SESSION_STATE.yaml's pixel-format decision). Formula
// lifted from imlib/yuv.c's own PIXFORMAT_RGB565 case (the standard
// integer BT.601-ish approximation OpenMV itself uses), rewritten to
// walk YUV_420_888's real 4:2:0 planar/semi-planar layout (imlib's own
// version targets a different, 4:2:2 interleaved layout -- not directly
// reusable, see SESSION_STATE.yaml) via AImage's plane accessors, which
// correctly abstract over planar vs. semi-planar U/V (pixelStride
// differs, getPlanePixelStride() handles either case transparently).
void convert_to_rgb565(AImage *image, image_t *out) {
    uint8_t *y_data = nullptr, *u_data = nullptr, *v_data = nullptr;
    int y_len = 0, u_len = 0, v_len = 0;
    int32_t y_row_stride = 0, u_row_stride = 0, v_row_stride = 0;
    int32_t u_pixel_stride = 0, v_pixel_stride = 0;
    AImage_getPlaneData(image, 0, &y_data, &y_len);
    AImage_getPlaneRowStride(image, 0, &y_row_stride);
    AImage_getPlaneData(image, 1, &u_data, &u_len);
    AImage_getPlaneRowStride(image, 1, &u_row_stride);
    AImage_getPlanePixelStride(image, 1, &u_pixel_stride);
    AImage_getPlaneData(image, 2, &v_data, &v_len);
    AImage_getPlaneRowStride(image, 2, &v_row_stride);
    AImage_getPlanePixelStride(image, 2, &v_pixel_stride);

    uint16_t *dst = (uint16_t *) out->data;
    for (int32_t row = 0; row < out->h; row++) {
        int32_t uv_row = row / 2;
        uint8_t *y_row_ptr = y_data + (size_t) row * y_row_stride;
        uint8_t *u_row_ptr = u_data + (size_t) uv_row * u_row_stride;
        uint8_t *v_row_ptr = v_data + (size_t) uv_row * v_row_stride;
        uint16_t *dst_row = dst + (size_t) row * out->w;

        for (int32_t col = 0; col < out->w; col++) {
            int y = y_row_ptr[col];
            int u = (int) u_row_ptr[(col / 2) * u_pixel_stride] - 128;
            int v = (int) v_row_ptr[(col / 2) * v_pixel_stride] - 128;

            // BT.601: R = Y + 1.402*Cr, B = Y + 1.772*Cb, G = Y -
            // 0.344*Cb - 0.714*Cr (179/128=1.402, 227/128=1.772,
            // 44/128=0.344, 91/128=0.714 -- the constants were already
            // right, u/v were swapped against them: R was getting Cb's
            // coefficient and B was getting Cr's, a clean red<->blue
            // swap. u=Cb (plane 1), v=Cr (plane 2), per AImage's
            // documented YUV_420_888 plane order -- confirmed real bug,
            // not a plane-index mixup, see SESSION_STATE.yaml.
            int ry = (179 * v) >> 7;
            int gy = ((44 * u) + (91 * v)) >> 7;
            int by = (227 * u) >> 7;

            int r = __USAT(y + ry, 8);
            int g = __USAT(y - gy, 8);
            int b = __USAT(y + by, 8);
            dst_row[col] = COLOR_R8_G8_B8_TO_RGB565(r, g, b);
        }
    }
}

mp_obj_t convert_image(AImage *image) {
    image_t img = {0};
    img.w = g_cam.width;
    img.h = g_cam.height;
    img.pixfmt = g_cam.pixfmt;
    image_alloc(&img, image_size(&img));

    if (g_cam.pixfmt == PIXFORMAT_RGB565) {
        convert_to_rgb565(image, &img);
    } else {
        convert_to_grayscale(image, &img);
    }
    return py_image_from_struct(&img);
}

// The one genuinely async wait in this whole module (see
// SESSION_STATE.yaml's csi.snapshot() chunked-wait decision) -- every
// other NDK call used here (openCamera, createCaptureSession) is
// documented as synchronous/blocking in its own right. A fresh sem_t per
// call, never reused (a stale sem_post() from a previous call's race
// could wake a later wait instantly) -- and nlr_push/nlr_pop-protected,
// since mp_handle_pending() raises via nlr_jump (setjmp/longjmp, no
// destructors) and the AImageReader callback must be unregistered BEFORE
// the semaphore is destroyed, or a late callback posts into freed
// memory -- a use-after-free, not a leak. Shared by both csi_snapshot's
// plain form and its time=/frames= warm-up loop (see below) -- same
// wait-for-one-frame primitive either way, not reimplemented per caller.
// Caller owns the returned AImage (AImage_delete() it).
AImage *wait_and_acquire_frame() {
    sem_t frame_sem;
    sem_init(&frame_sem, 0, 0);
    AImageReader_ImageListener listener = {&frame_sem, on_image_available};
    AImageReader_setImageListener(g_cam.reader, &listener);
    g_cam.active_wait_sem = &frame_sem;

    AImage *image = nullptr;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        bool got_frame = false;
        for (long waited = 0; waited < kSnapshotTimeoutMs; waited += kWaitChunkMs) {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_nsec += kWaitChunkMs * 1000000L;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec += 1;
                deadline.tv_nsec -= 1000000000L;
            }
            if (sem_timedwait(&frame_sem, &deadline) == 0) {
                got_frame = true;
                break;
            }
            // Raises (nlr_jump into the `else` branch below) if the user
            // tapped Interrupt while we were waiting.
            mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
        }

        if (!got_frame) {
            raise_os_error(MP_ETIMEDOUT, "camera snapshot timed out");
        }
        if (AImageReader_acquireNextImage(g_cam.reader, &image) != AMEDIA_OK || image == nullptr) {
            raise_os_error(MP_EIO, "camera: failed to acquire frame");
        }
        nlr_pop();
    } else {
        AImageReader_setImageListener(g_cam.reader, nullptr);
        g_cam.active_wait_sem = nullptr;
        sem_destroy(&frame_sem);
        nlr_jump(nlr.ret_val);
    }

    AImageReader_setImageListener(g_cam.reader, nullptr);
    g_cam.active_wait_sem = nullptr;
    sem_destroy(&frame_sem);
    return image;
}

long now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long) ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

// time=/frames= form: discards frames for up to `time` ms and/or
// `frames` count (whichever limit is reached first -- both may be
// given), returns None. A sensor-settle/AGC-AWB-warmup idiom real
// OpenMV scripts use before their real capture loop starts (see
// SESSION_STATE.yaml -- confirmed by reading py_csi_snapshot() directly,
// NOT the same mechanism as this module's own 500ms per-call timeout-as-
// error). Empirically confirmed to matter on real hardware, not just a
// theoretical gap: a snapshot() immediately after reset() measured a
// sampled mean of ~35/255 on a normally-lit scene; by frame 6-7 of a
// discard loop the same scene measured ~147/255 -- real AE convergence,
// not a conversion bug.
void csi_snapshot_warmup(mp_int_t time_limit_ms, mp_int_t frames_limit) {
    long start = now_ms();
    int n = 0;
    while (true) {
        if (time_limit_ms >= 0 && (now_ms() - start) >= time_limit_ms) {
            break;
        }
        if (frames_limit >= 0 && n >= frames_limit) {
            break;
        }
        AImage *image = wait_and_acquire_frame();
        AImage_delete(image);
        n++;
    }
}

mp_obj_t csi_snapshot(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_time, ARG_frames };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_time, MP_ARG_INT | MP_ARG_KW_ONLY, {.u_int = -1}},
        {MP_QSTR_frames, MP_ARG_INT | MP_ARG_KW_ONLY, {.u_int = -1}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (!g_cam.device) {
        raise_os_error(MP_EINVAL, "camera not reset -- call reset() first");
    }
    ensure_session();

    if (args[ARG_time].u_int >= 0 || args[ARG_frames].u_int >= 0) {
        csi_snapshot_warmup(args[ARG_time].u_int, args[ARG_frames].u_int);
        return mp_const_none;
    }

    AImage *image = wait_and_acquire_frame();
    mp_obj_t result = convert_image(image);
    AImage_delete(image);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(csi_snapshot_obj, 1, csi_snapshot);

mp_obj_t csi_reset(mp_obj_t self_in) {
    (void) self_in;
    camera_close_all();
    // Real hardware/OpenMV semantics: reset() re-initializes to power-on
    // defaults, not just "reopen with whatever was last configured" --
    // g_cam is file-scope state that outlives any single Python CSI
    // object (and mp_embed_deinit()/mp_embed_init() only reset
    // MicroPython/GC state, never this module's own globals), so
    // without this a script's `csi0.reset()` after an earlier RGB565
    // script/REPL session would silently start from RGB565, not
    // GRAYSCALE. Caught on-device: a second run after tapping Reset
    // inherited the previous run's pixformat(RGB565) call.
    g_cam.pixfmt = PIXFORMAT_GRAYSCALE;
    g_cam.width = kDefaultWidth;
    g_cam.height = kDefaultHeight;

    g_cam.manager = ACameraManager_create();
    if (!g_cam.manager) {
        raise_os_error(MP_EIO, "camera: failed to create manager");
    }

    ACameraIdList *ids = nullptr;
    if (ACameraManager_getCameraIdList(g_cam.manager, &ids) != ACAMERA_OK || !ids || ids->numCameras == 0) {
        raise_os_error(MP_ENODEV, "camera: no camera available");
    }

    ACameraDevice_stateCallbacks device_cb = {nullptr, on_device_disconnected, on_device_error};
    camera_status_t st = ACameraManager_openCamera(g_cam.manager, ids->cameraIds[0], &device_cb, &g_cam.device);
    ACameraManager_deleteCameraIdList(ids);

    if (st != ACAMERA_OK) {
        ACameraManager_delete(g_cam.manager);
        g_cam.manager = nullptr;
        if (st == ACAMERA_ERROR_PERMISSION_DENIED) {
            raise_os_error(MP_EACCES, "camera access denied -- open Settings and grant Camera permission");
        }
        raise_os_error(MP_EIO, "camera: failed to open");
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(csi_reset_obj, csi_reset);

mp_obj_t csi_pixformat(mp_obj_t self_in, mp_obj_t fmt_in) {
    (void) self_in;
    int fmt = mp_obj_get_int(fmt_in);
    if (fmt != PIXFORMAT_GRAYSCALE && fmt != PIXFORMAT_RGB565) {
        mp_raise_ValueError(MP_ERROR_TEXT("unsupported pixformat"));
    }
    if ((pixformat_t) fmt != g_cam.pixfmt) {
        g_cam.pixfmt = (pixformat_t) fmt;
        close_session_and_reader(); // rebuilt lazily by the next snapshot()
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(csi_pixformat_obj, csi_pixformat);

// Deliberately does NOT validate w/h against the camera's real
// supported sizes (see csi_framesize_list() below) -- an unsupported
// size is silently passed straight to AImageReader_new()/the capture
// session. On real hardware this does not fail outright; it can
// deliver a frame that doesn't fully populate the requested buffer,
// which (fed through the YUV->RGB conversion) reads as a solid green
// band wherever the unwritten rows/planes land. Confirmed on-device
// (device kunzite_eea): requesting csi.CSI().framesize((128, 160)) --
// not a supported size on that camera -- produced exactly this artifact
// at a fixed position every frame; switching to (176, 144), a size
// confirmed present in that camera's own StreamConfigurationMap, made
// it disappear completely. Deliberately not turned into a raised error
// here (unlike, e.g., py_imu.c's temperature_c() omission) -- Android
// camera hardware varies per device in a way OpenMV's own fixed,
// known-at-build-time sensor never does, so silently accepting
// whatever a script asks for and letting csi_framesize_list() answer
// "what's actually valid on THIS device" was judged more useful than a
// hardcoded validation table here. Scripts that want the safety net
// should check against framesize_list() themselves before calling this.
mp_obj_t csi_framesize(mp_obj_t self_in, mp_obj_t size_in) {
    (void) self_in;
    int32_t w, h;
    if (mp_obj_is_type(size_in, &mp_type_tuple)) {
        size_t len;
        mp_obj_t *items;
        mp_obj_tuple_get(size_in, &len, &items);
        if (len != 2) {
            mp_raise_ValueError(MP_ERROR_TEXT("framesize tuple must be (width, height)"));
        }
        w = mp_obj_get_int(items[0]);
        h = mp_obj_get_int(items[1]);
    } else {
        // Named constants (csi.QQVGA etc, see the module dict below) are
        // packed as (w << 16) | h -- self-describing, no lookup table.
        int packed = mp_obj_get_int(size_in);
        w = packed >> 16;
        h = packed & 0xFFFF;
    }
    if (w <= 0 || h <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid framesize"));
    }
    if (w != g_cam.width || h != g_cam.height) {
        g_cam.width = w;
        g_cam.height = h;
        close_session_and_reader(); // rebuilt lazily by the next snapshot()
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(csi_framesize_obj, csi_framesize);

// Returns [(w, h), ...] -- every YUV_420_888 OUTPUT size this specific
// device's camera 0 actually supports (ACAMERA_SCALER_
// AVAILABLE_STREAM_CONFIGURATIONS, filtered to the one format this
// port ever captures), sorted smallest-first by pixel count. Exists
// because Android camera hardware varies per device in a way OpenMV's
// own fixed sensor never does -- there's no compile-time list of valid
// sizes to hardcode, so a script has to ask the actual device. See
// csi_framesize()'s own comment for what happens if an unsupported
// size is used anyway (accepted silently, can corrupt part of the
// frame -- this method is how a script avoids that, not a requirement
// enforced here).
mp_obj_t csi_framesize_list(mp_obj_t self_in) {
    (void) self_in;
    if (!g_cam.device) {
        raise_os_error(MP_EINVAL, "camera not reset -- call reset() first");
    }

    ACameraIdList *ids = nullptr;
    if (ACameraManager_getCameraIdList(g_cam.manager, &ids) != ACAMERA_OK || !ids || ids->numCameras == 0) {
        raise_os_error(MP_ENODEV, "camera: no camera available");
    }
    ACameraMetadata *metadata = nullptr;
    camera_status_t st = ACameraManager_getCameraCharacteristics(g_cam.manager, ids->cameraIds[0], &metadata);
    ACameraManager_deleteCameraIdList(ids);
    if (st != ACAMERA_OK || !metadata) {
        raise_os_error(MP_EIO, "camera: failed to read characteristics");
    }

    ACameraMetadata_const_entry entry = {};
    st = ACameraMetadata_getConstEntry(metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
    if (st != ACAMERA_OK) {
        ACameraMetadata_free(metadata);
        raise_os_error(MP_EIO, "camera: no stream configuration info");
    }

    // Each group of 4 int32s: format, width, height, input(1)/output(0).
    std::vector<std::pair<int32_t, int32_t>> sizes;
    for (uint32_t i = 0; i + 3 < entry.count; i += 4) {
        int32_t format = entry.data.i32[i];
        int32_t width = entry.data.i32[i + 1];
        int32_t height = entry.data.i32[i + 2];
        int32_t io = entry.data.i32[i + 3];
        if (format == AIMAGE_FORMAT_YUV_420_888 && io == ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) {
            sizes.emplace_back(width, height);
        }
    }
    ACameraMetadata_free(metadata);

    std::sort(sizes.begin(), sizes.end(), [](const std::pair<int32_t, int32_t> &a, const std::pair<int32_t, int32_t> &b) {
        int64_t area_a = (int64_t) a.first * a.second;
        int64_t area_b = (int64_t) b.first * b.second;
        if (area_a != area_b) {
            return area_a < area_b;
        }
        return a.first < b.first;
    });

    mp_obj_t list = mp_obj_new_list(0, nullptr);
    for (const auto &wh : sizes) {
        mp_obj_t tuple[2] = {MP_OBJ_NEW_SMALL_INT(wh.first), MP_OBJ_NEW_SMALL_INT(wh.second)};
        mp_obj_list_append(list, mp_obj_new_tuple(2, tuple));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(csi_framesize_list_obj, csi_framesize_list);

mp_obj_t csi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void) n_args;
    (void) n_kw;
    (void) args;
    csi_obj_t *self = m_new_obj(csi_obj_t);
    self->base.type = type;
    return MP_OBJ_FROM_PTR(self);
}

const mp_rom_map_elem_t csi_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&csi_reset_obj)},
    {MP_ROM_QSTR(MP_QSTR_pixformat), MP_ROM_PTR(&csi_pixformat_obj)},
    {MP_ROM_QSTR(MP_QSTR_framesize), MP_ROM_PTR(&csi_framesize_obj)},
    {MP_ROM_QSTR(MP_QSTR_framesize_list), MP_ROM_PTR(&csi_framesize_list_obj)},
    {MP_ROM_QSTR(MP_QSTR_snapshot), MP_ROM_PTR(&csi_snapshot_obj)},
};
MP_DEFINE_CONST_DICT(csi_locals_dict, csi_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    csi_type,
    MP_QSTR_CSI,
    MP_TYPE_FLAG_NONE,
    make_new, csi_make_new,
    locals_dict, &csi_locals_dict
    );

#define FRAMESIZE_PACK(w, h) (((w) << 16) | (h))

const mp_rom_map_elem_t csi_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_csi)},
    {MP_ROM_QSTR(MP_QSTR_CSI), MP_ROM_PTR(&csi_type)},
    {MP_ROM_QSTR(MP_QSTR_GRAYSCALE), MP_ROM_INT(PIXFORMAT_GRAYSCALE)},
    {MP_ROM_QSTR(MP_QSTR_RGB565), MP_ROM_INT(PIXFORMAT_RGB565)},
    {MP_ROM_QSTR(MP_QSTR_QQVGA), MP_ROM_INT(FRAMESIZE_PACK(160, 120))},
    {MP_ROM_QSTR(MP_QSTR_QVGA), MP_ROM_INT(FRAMESIZE_PACK(320, 240))},
    {MP_ROM_QSTR(MP_QSTR_VGA), MP_ROM_INT(FRAMESIZE_PACK(640, 480))},
};
MP_DEFINE_CONST_DICT(csi_module_globals, csi_module_globals_table);

} // namespace

// extern "C" (not inside the anonymous namespace above, unlike
// everything else in this file) -- the generated genhdr/moduledefs.h
// declares `extern const struct _mp_obj_module_t csi_module;` with C
// linkage, since MicroPython's own module machinery is plain C. An
// anonymous-namespace definition would have internal linkage and the
// real declaration could never bind to it, a link error only, not a
// compile error (caught building the real app, not the qstr scan).
extern "C" const mp_obj_module_t csi_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &csi_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_csi, csi_module);

extern "C" void camera_close_all(void) {
    close_session_and_reader();
    if (g_cam.device) {
        ACameraDevice_close(g_cam.device);
        g_cam.device = nullptr;
    }
    if (g_cam.manager) {
        ACameraManager_delete(g_cam.manager);
        g_cam.manager = nullptr;
    }
}

extern "C" void camera_interrupt_active_wait(void) {
    if (g_cam.active_wait_sem) {
        sem_post(g_cam.active_wait_sem);
    }
}
