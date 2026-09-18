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
#include <string>
#include <cstdlib>

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
    // Resolved at reset() time (see csi_reset()), consumed by
    // csi_framesize_list()/android_light_set()/android_zoom_* instead
    // of each re-fetching ids->cameraIds[0] independently (three
    // separate copies of that fetch before this field existed) --
    // owns its own copy since ACameraIdList's strings are freed by
    // ACameraManager_deleteCameraIdList() right after reset() reads
    // them, they don't outlive the call the way this field needs to.
    std::string camera_id;
    // Requested camera ID (raw, as returned by csi.CSI().camera_list()
    // -- see that function's own comment), or -1 for "unspecified" --
    // matches OpenMV's own py_csi_ng.c cid default exactly. Set by
    // csi_make_new()'s cid= kwarg, consumed by csi_reset(). -1
    // preserves this port's original, pre-cid behavior byte-for-byte
    // (ids->cameraIds[0], whatever the device lists first) rather than
    // silently changing existing scripts' behavior. Deliberately NOT
    // resolved by facing/semantics (no FRONT/BACK constant on this
    // module) -- user's own call: mirror Android's own raw camera IDs
    // directly, a script builds its own FRONT/BACK naming from
    // camera_list()'s facing info if it wants that.
    int32_t requested_cid;
    // Set only while a snapshot() call is actively waiting for a frame --
    // camera_interrupt_active_wait() (called from nativeInterrupt(), any
    // thread) posts into this if non-null. Written only from the worker
    // thread except for that one post, which is what sem_post() is for.
    sem_t *active_wait_sem;
};

CameraState g_cam = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    kDefaultWidth, kDefaultHeight, PIXFORMAT_GRAYSCALE, "", -1, nullptr,
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

// Mirrors imlib's own image_alloc() (lib/imlib/imlib.c, pristine
// vendored -- see NOTICE.md, never patched) exactly: same over-allocate
// + round-up-the-pointer technique, same m_malloc() backing allocator
// -- deliberately NOT posix_memalign/plain malloc. image_t._raw's own
// struct comment (imlib.h) says it "keeps a reference to the GC block
// when used with image_alloc/image_alloc0" -- switching to a libc-heap
// allocator here would silently break that (the GC would never see or
// reclaim the buffer), needing a whole new finalizer-based lifecycle to
// fix correctly. Confirmed by reading image_alloc()'s real body
// directly, not assumed -- an earlier pass this session had incorrectly
// inferred a plain-malloc chain without checking this (see
// SESSION_STATE.yaml).
//
// Only alignment target differs: 64 bytes (TfLiteInterpreterSetCustom
// AllocationForTensor's own documented requirement, see SESSION_STATE.
// yaml's android.tf design discussion), not OMV_CACHE_LINE_SIZE (32 on
// this port, confirmed -- __DCACHE_PRESENT is never defined in this
// port's own CMSIS stub headers, so omv_common.h's plain 32-byte branch
// is the one actually compiled in). A stricter alignment than the
// pristine default is harmless for every other use of a captured image,
// so this replaces image_alloc() for ALL csi.snapshot() results here,
// not a separate ML-only capture path -- every snapshot becomes usable
// for future zero-copy tensor aliasing with no new API surface.
constexpr size_t kTfAlignment = 64;

void image_alloc_tf_aligned(image_t *img, size_t size) {
    size_t aligned_size = (size + kTfAlignment - 1) & ~(kTfAlignment - 1);
    img->_raw = (uint8_t *) m_malloc(aligned_size + kTfAlignment - 1);
    img->data = (uint8_t *) (((uintptr_t) img->_raw + kTfAlignment - 1) & ~((uintptr_t) (kTfAlignment - 1)));
    // Permanent, cheap sanity check on the alignment math above --
    // silent when correct, LOGW's if the invariant this whole function
    // exists for is ever actually violated (would mean a real bug here,
    // not something to discover only later as a mysterious
    // TfLiteInterpreterSetCustomAllocationForTensor failure downstream).
    if (((uintptr_t) img->data) % kTfAlignment != 0) {
        LOGW("image_alloc_tf_aligned: data=%p is NOT %zu-byte aligned (bug)", img->data, kTfAlignment);
    }
}

mp_obj_t convert_image(AImage *image) {
    image_t img = {0};
    img.w = g_cam.width;
    img.h = g_cam.height;
    img.pixfmt = g_cam.pixfmt;
    image_alloc_tf_aligned(&img, image_size(&img));

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

    // requested_cid == -1 (default, no cid= given): preserves this
    // port's original behavior byte-for-byte, ids->cameraIds[0]
    // unconditionally -- whatever the device lists first, same as
    // before csi.CSI(cid=...) existed at all. Otherwise: the raw ID is
    // used directly (stringified), deliberately unvalidated against
    // the ids list here -- mirrors framesize()'s own established
    // "silently accepts, real failure surfaces naturally" precedent; a
    // bad cid= surfaces as the ordinary ACameraManager_openCamera()
    // failure path just below, not a separate up-front check.
    g_cam.camera_id = (g_cam.requested_cid == -1) ? ids->cameraIds[0] : std::to_string(g_cam.requested_cid);

    ACameraDevice_stateCallbacks device_cb = {nullptr, on_device_disconnected, on_device_error};
    camera_status_t st = ACameraManager_openCamera(g_cam.manager, g_cam.camera_id.c_str(), &device_cb, &g_cam.device);
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

    // g_cam.camera_id (resolved by csi_reset(), see CameraState's own
    // comment) -- the currently-open camera, not necessarily
    // cameraIds[0] anymore now that cid= selection exists.
    ACameraMetadata *metadata = nullptr;
    camera_status_t st = ACameraManager_getCameraCharacteristics(g_cam.manager, g_cam.camera_id.c_str(), &metadata);
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

// csi.CSI().camera_list() -- real per-device camera enumeration, same
// discovery-API pattern as framesize_list() (paired with the raw cid=
// selection csi_make_new() accepts, same way framesize_list() pairs
// with framesize()). Returns [(id, facing, has_flash), ...] for every
// camera ACameraManager_getCameraIdList() reports -- facing is the raw
// ACAMERA_LENS_FACING value (0=front, 1=back, 2=external -- Android's
// own real enum, not a label this module invents; -1 if the query
// itself fails for a given camera), has_flash is a bool. Deliberately
// no FRONT/BACK naming here either -- user's own call: a script builds
// its own naming from this data if it wants that. Uses its own
// throwaway ACameraManager rather than g_cam.manager, so it works
// whether or not reset() has ever been called -- the whole point is
// letting a script decide which cid= to reset() with in the first
// place, so requiring an existing session would be circular.
mp_obj_t csi_camera_list(mp_obj_t self_in) {
    (void) self_in;
    ACameraManager *manager = ACameraManager_create();
    if (!manager) {
        raise_os_error(MP_EIO, "camera: failed to create manager");
    }
    ACameraIdList *ids = nullptr;
    if (ACameraManager_getCameraIdList(manager, &ids) != ACAMERA_OK || !ids) {
        ACameraManager_delete(manager);
        raise_os_error(MP_ENODEV, "camera: no camera available");
    }

    mp_obj_t list = mp_obj_new_list(0, nullptr);
    for (int i = 0; i < ids->numCameras; i++) {
        ACameraMetadata *metadata = nullptr;
        if (ACameraManager_getCameraCharacteristics(manager, ids->cameraIds[i], &metadata) != ACAMERA_OK || !metadata) {
            continue;
        }

        ACameraMetadata_const_entry facing_entry = {};
        camera_status_t fst = ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &facing_entry);
        int32_t facing = (fst == ACAMERA_OK && facing_entry.count > 0) ? facing_entry.data.u8[0] : -1;

        ACameraMetadata_const_entry flash_entry = {};
        camera_status_t hst = ACameraMetadata_getConstEntry(metadata, ACAMERA_FLASH_INFO_AVAILABLE, &flash_entry);
        bool has_flash = (hst == ACAMERA_OK && flash_entry.count > 0 && flash_entry.data.u8[0] != 0);
        ACameraMetadata_free(metadata);

        mp_obj_t tuple[3] = {
            mp_obj_new_int(atoi(ids->cameraIds[i])),
            mp_obj_new_int(facing),
            has_flash ? mp_const_true : mp_const_false,
        };
        mp_obj_list_append(list, mp_obj_new_tuple(3, tuple));
    }
    ACameraManager_deleteCameraIdList(ids);
    ACameraManager_delete(manager);
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(csi_camera_list_obj, csi_camera_list);

// cid= matches OpenMV's own py_csi_ng.c constructor kwarg exactly (name
// and -1 "unspecified" default) -- real precedent for camera selection
// on boards with more than one sensor (csi.CSI(cid=csi.LEPTON) for a
// FLIR thermal camera alongside the visible one, csi.CSI(cid=
// csi.GENX320) for an event camera). Unlike OpenMV's own fixed,
// compile-time-known board hardware, this is a raw camera ID (as
// returned by csi.CSI().camera_list(), see that function's own
// comment) -- deliberately NOT a semantic FRONT/BACK constant: user's
// own call, mirror Android's own camera IDs directly rather than
// impose a labeling scheme here; a script builds its own FRONT/BACK
// naming from camera_list()'s facing info if it wants that. See
// SESSION_STATE.yaml's "android module" design discussion.
mp_obj_t csi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_cid };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_cid, MP_ARG_INT | MP_ARG_KW_ONLY, {.u_int = -1}},
    };
    // make_new()'s own calling convention (n_args/n_kw/args, kwargs
    // interleaved into args rather than a separate mp_map_t) needs
    // mp_arg_parse_all_kw_array specifically -- same call shape
    // display_make_new() already uses in display_module.cpp.
    mp_arg_val_t kw_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, kw_args);
    g_cam.requested_cid = kw_args[ARG_cid].u_int;

    csi_obj_t *self = m_new_obj(csi_obj_t);
    self->base.type = type;
    return MP_OBJ_FROM_PTR(self);
}

const mp_rom_map_elem_t csi_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&csi_reset_obj)},
    {MP_ROM_QSTR(MP_QSTR_pixformat), MP_ROM_PTR(&csi_pixformat_obj)},
    {MP_ROM_QSTR(MP_QSTR_framesize), MP_ROM_PTR(&csi_framesize_obj)},
    {MP_ROM_QSTR(MP_QSTR_framesize_list), MP_ROM_PTR(&csi_framesize_list_obj)},
    {MP_ROM_QSTR(MP_QSTR_camera_list), MP_ROM_PTR(&csi_camera_list_obj)},
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

// android.light.on()/off() -- torch, tied to the active csi capture
// session rather than a standalone flashlight (user's own call: the
// light shield's real purpose is illuminating a scene FOR the camera,
// same device the shield is physically mounted on, so this constraint
// matches actual use rather than costing real capability -- see
// SESSION_STATE.yaml's "android module" design discussion). No
// standalone NDK path exists for torch outside an active capture
// session, checked directly against the newest installed NDK
// (30.0.16248370, API annotations up to 36, the current one as of this
// writing): NdkCameraMetadataTags.h's own ACAMERA_FLASH_TORCH_STRENGTH_
// MAX_LEVEL doc comment points at Java's CameraManager#
// turnOnTorchWithStrengthLevel as the real standalone API -- there is
// no NDK equivalent, at any API level. ACAMERA_FLASH_MODE=TORCH on the
// existing repeating request is the only native mechanism.
//
// Declared outside the anonymous namespace above (unlike every other
// function in this file except camera_close_all()/
// camera_interrupt_active_wait()) so android_module.cpp can reference
// the function objects by extern -- and, specifically, `extern
// MP_DEFINE_CONST_FUN_OBJ_0(...)` rather than the plain `static
// MP_DEFINE_CONST_FUN_OBJ_0(...)` every csi.* function here uses: in
// C++, a `const` global has INTERNAL linkage by default (unlike C), so
// without the explicit `extern` the object would silently fail to link
// from another translation unit (hit this for real building
// android.proximity.distance_cm() in imu_module.cpp, see its own
// comment there).
mp_obj_t android_light_set(bool on) {
    if (!g_cam.device) {
        raise_os_error(MP_EINVAL, "android.light: camera not reset -- call csi.CSI().reset() first");
    }
    ensure_session();

    // g_cam.camera_id (resolved by csi_reset()) -- whichever camera is
    // actually active right now (front or back, if cid= was used) --
    // best-effort, per-call: does NOT try to remember torch state
    // across a later reset()/camera switch, and never verifies the LED
    // actually illuminated (ACAMERA_FLASH_STATE, the only signal for
    // that, is only reported asynchronously in capture RESULTS, which
    // this port doesn't read back at all) -- see SESSION_STATE.yaml's
    // "android module" design discussion.
    ACameraMetadata *metadata = nullptr;
    camera_status_t cst = ACameraManager_getCameraCharacteristics(g_cam.manager, g_cam.camera_id.c_str(), &metadata);
    if (cst != ACAMERA_OK || !metadata) {
        raise_os_error(MP_EIO, "android.light: failed to read camera characteristics");
    }
    ACameraMetadata_const_entry entry = {};
    camera_status_t st = ACameraMetadata_getConstEntry(metadata, ACAMERA_FLASH_INFO_AVAILABLE, &entry);
    bool has_flash = (st == ACAMERA_OK && entry.count > 0 && entry.data.u8[0] != 0);
    ACameraMetadata_free(metadata);
    if (!has_flash) {
        raise_os_error(MP_ENODEV, "android.light: no flash unit on this device's camera");
    }

    // Observed on real hardware (this device's HAL, user's own
    // diagnosis, confirmed live): turning the torch ON this way worked
    // and was visually confirmed; turning it back OFF the same way did
    // NOT -- the physical LED stayed lit until a full csi.CSI().reset()
    // (closing and reopening the camera device) forced it off. Likely
    // cause: this HAL only re-evaluates ACAMERA_FLASH_MODE at the start
    // of a repeating request, not on a live in-place update. Left
    // exactly as the plain NDK call sequence anyway -- user's own call:
    // this module mirrors the real Android NDK API directly rather than
    // working around individual HAL quirks in code; a script that hits
    // this can always fall back to csi.CSI().reset() itself.
    uint8_t mode = on ? ACAMERA_FLASH_MODE_TORCH : ACAMERA_FLASH_MODE_OFF;
    if (ACaptureRequest_setEntry_u8(g_cam.request, ACAMERA_FLASH_MODE, 1, &mode) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "android.light: failed to set flash mode");
    }
    if (ACameraCaptureSession_setRepeatingRequest(g_cam.session, nullptr, 1, &g_cam.request, nullptr) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "android.light: failed to apply flash mode");
    }
    return mp_const_none;
}

mp_obj_t android_light_on() {
    return android_light_set(true);
}
extern MP_DEFINE_CONST_FUN_OBJ_0(android_light_on_obj, android_light_on);

mp_obj_t android_light_off() {
    return android_light_set(false);
}
extern MP_DEFINE_CONST_FUN_OBJ_0(android_light_off_obj, android_light_off);

// android.zoom.set(ratio)/range() -- tied to the active csi capture
// session, same reasoning/constraint as android.light (a zoom ratio is
// meaningless without an open camera device to apply it to). set()
// is deliberately unvalidated -- mirrors csi.CSI().framesize()'s own
// established "silently accepts, hardware clamps/ignores out-of-range"
// precedent exactly, not a new design choice. range() is the discovery
// half, same framesize()/framesize_list() split -- queries
// ACAMERA_CONTROL_ZOOM_RATIO_RANGE on the currently-open camera
// (g_cam.camera_id) and returns (1.0, 1.0), not an error, if the tag
// is absent (some cameras/devices only support the older crop-region-
// based digital zoom, not ratio-based zoom at all -- "no zoom range"
// is a valid, honest answer here, not a malfunction).
mp_obj_t android_zoom_set(mp_obj_t ratio_in) {
    if (!g_cam.device) {
        raise_os_error(MP_EINVAL, "android.zoom: camera not reset -- call csi.CSI().reset() first");
    }
    ensure_session();
    float ratio = mp_obj_get_float(ratio_in);
    if (ACaptureRequest_setEntry_float(g_cam.request, ACAMERA_CONTROL_ZOOM_RATIO, 1, &ratio) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "android.zoom: failed to set zoom ratio");
    }
    if (ACameraCaptureSession_setRepeatingRequest(g_cam.session, nullptr, 1, &g_cam.request, nullptr) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "android.zoom: failed to apply zoom ratio");
    }
    return mp_const_none;
}
extern MP_DEFINE_CONST_FUN_OBJ_1(android_zoom_set_obj, android_zoom_set);

mp_obj_t android_zoom_range() {
    if (!g_cam.device) {
        raise_os_error(MP_EINVAL, "android.zoom: camera not reset -- call csi.CSI().reset() first");
    }
    ACameraMetadata *metadata = nullptr;
    if (ACameraManager_getCameraCharacteristics(g_cam.manager, g_cam.camera_id.c_str(), &metadata) != ACAMERA_OK || !metadata) {
        raise_os_error(MP_EIO, "android.zoom: failed to read camera characteristics");
    }
    ACameraMetadata_const_entry entry = {};
    camera_status_t st = ACameraMetadata_getConstEntry(metadata, ACAMERA_CONTROL_ZOOM_RATIO_RANGE, &entry);
    float min_ratio = 1.0f, max_ratio = 1.0f;
    if (st == ACAMERA_OK && entry.count >= 2) {
        min_ratio = entry.data.f[0];
        max_ratio = entry.data.f[1];
    }
    ACameraMetadata_free(metadata);
    mp_obj_t tuple[2] = {mp_obj_new_float(min_ratio), mp_obj_new_float(max_ratio)};
    return mp_obj_new_tuple(2, tuple);
}
extern MP_DEFINE_CONST_FUN_OBJ_0(android_zoom_range_obj, android_zoom_range);

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
