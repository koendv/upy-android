// upy-android native csi (camera) module.
// bridge between py_image.c's Image type and Android's NDK Camera2 API
// see session-state: camera_module.cpp#module_design

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
#include <atomic>
#include <vector>
#include <algorithm>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/nlr.h"
#include "py/mperrno.h"
#include "imlib.h"
#include "py_image.h"
#include "framebuffer.h"
}

#define LOG_TAG "upy-camera"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

// Same 5ms interrupt-check granularity mp_hal_delay_ms() uses, reused so
// interrupt responsiveness stays uniform across every blocking call.
constexpr long kWaitChunkMs = 5;
// Per-frame stuck-camera timeout inside csi_snapshot_warmup()'s frames=
// wait, and the steady-state drain-one-frame wait inside csi_snapshot()
// itself (see wait_for_frame()) -- both cases where a frame is expected
// imminently (the camera's already warm and streaming), so a fast
// timeout is correct and desirable there.
constexpr long kSnapshotTimeoutMs = 500;
// Generous budget for the FIRST frame specifically -- only reached once
// per session, right after reset()/a pixformat()/framesize()/
// framebuffers() rebuild, before on_image_available() has fired even
// once (see wait_for_frame()). Confirmed on-device this needs more than
// kSnapshotTimeoutMs=500 covers: closing an actively-streaming session
// and immediately reopening (e.g. reset() called back-to-back, or right
// after a long snapshot loop) can genuinely take Camera2's HAL longer
// than 500ms to produce a first frame on real hardware -- this is a
// real, measured hardware/HAL characteristic, not a protocol bug (the
// old design had the exact same 500ms budget for every single
// snapshot() call, just less likely to be visibly hit since a warm
// camera almost always beat it; this design concentrates that one
// slower case into a single, rare, one-time wait per session, so a
// larger budget here costs nothing in the steady state).
constexpr long kFirstFrameTimeoutMs = 3000;

// QVGA. Real OpenMV boards default to PIXFORMAT_INVALID/no framesize,
// requiring both to be set explicitly before snapshot(); this port
// defaults to something usable so an early snapshot() still produces a
// real image rather than an error.
constexpr int32_t kDefaultWidth = 320;
constexpr int32_t kDefaultHeight = 240;

// Default double-buffer -- see csi_framebuffers() below, mirrors real
// OpenMV's own sensor.framebuffers(n).
constexpr size_t kDefaultBufCount = 2;

// special_effect() values, same numbers as OpenMV's OMV_CSI_SDE_*.
constexpr int kSdeNormal = 0;
constexpr int kSdeNegative = 1;

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
    // Resolved at reset() time, consumed instead of each caller
    // re-fetching ids->cameraIds[0] independently. Owns its own copy
    // since ACameraIdList's strings are freed by
    // ACameraManager_deleteCameraIdList() right after reset() reads them.
    std::string camera_id;
    // -1 means "unspecified". see session-state: camera_module.cpp#camera_id_selection
    int32_t requested_cid;
    // Persistent for the whole process, not per-snapshot()-call: the
    // image-available callback (an Android-internal thread, permanently
    // registered -- see ensure_session()/on_image_available() below)
    // posts here on every successfully-written frame. wait_for_frame()
    // and csi_snapshot_warmup() block on it; camera_interrupt_active_wait()
    // (any thread) posts into it too, to unblock an interruptible wait.
    // Lazily sem_init'd once (persistent_frame_sem_ready), never
    // sem_destroy'd -- process lifetime, like g_cam itself.
    sem_t persistent_frame_sem;
    bool persistent_frame_sem_ready;
    sem_t *active_wait_sem;
    // Bumped on every close_session_and_reader() call; on_image_available()
    // discards a callback whose FrameContext generation doesn't match the
    // current one -- a straggler from an already-torn-down reader. See
    // close_session_and_reader()'s own comment for the full teardown
    // ordering this depends on.
    std::atomic<uint64_t> generation;
    // Incremented at on_image_available() entry, decremented at exit
    // (every path). close_session_and_reader() spins on this being 0
    // before freeing/reassigning anything the callback could still be
    // touching -- the generation check alone is a race without this.
    std::atomic<int> callback_in_flight;
    // Configured buffer count for the real, ported OpenMV framebuffer_t
    // (FB_MAINFB_ID) -- see csi_framebuffers() below. 1/2/3, matching
    // real OpenMV's own single/double/triple buffering.
    size_t buf_count;
    // Incremented once per successfully-written frame (on_image_available()
    // only) -- csi_snapshot_warmup()'s frames=N wait reads this.
    std::atomic<unsigned long> frame_seq;
};

CameraState g_cam = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    kDefaultWidth, kDefaultHeight, PIXFORMAT_GRAYSCALE, "", -1,
    {}, false, nullptr, {0}, {0}, kDefaultBufCount, {0},
};

// Capture request controls set by csi's OpenMV-named methods (auto_gain(),
// brightness(), ...) and android.light/zoom. Kept here, not only in
// g_cam.request, because the request is freed on every session rebuild
// (pixformat()/framesize()/framebuffers()); apply_controls() writes them
// into each new request. Cleared by reset(), like OpenMV's own reset().
struct CameraControls {
    bool auto_gain = true;
    bool auto_exposure = true;
    bool auto_whitebal = true;
    int32_t ev_index = 0;       // AE exposure compensation, in AE_COMPENSATION_STEP units
    int32_t fps_min = 0;        // 0: no AE target fps range requested
    int32_t fps_max = 0;
    uint8_t effect = ACAMERA_CONTROL_EFFECT_MODE_OFF;
    bool colorbar = false;
    bool torch = false;
    float zoom = 0.0f;          // 0: not set, HAL default
};

CameraControls g_ctl;

// Software geometry, applied while converting a frame in csi_snapshot():
// Camera2 has no mirror/flip controls, and its crop region zooms back to
// the output size where OpenMV's window() shrinks the output. Order as on
// OpenMV: hmirror/vflip act on the whole frame, window() crops that
// (coordinates as seen after mirror/flip), transpose() swaps the result's
// axes. Cleared by reset(); framesize() clears the window.
struct CameraGeometry {
    bool hmirror = false;
    bool vflip = false;
    bool transpose = false;
    bool windowed = false;          // false: whole frame, win_* all 0
    int32_t win_x = 0, win_y = 0;
    int32_t win_w = 0, win_h = 0;
};

CameraGeometry g_geo;

// Size of the image snapshot() returns.
void output_size(int32_t *w, int32_t *h) {
    int32_t ww = g_geo.windowed ? g_geo.win_w : g_cam.width;
    int32_t wh = g_geo.windowed ? g_geo.win_h : g_cam.height;
    *w = g_geo.transpose ? wh : ww;
    *h = g_geo.transpose ? ww : wh;
}

typedef struct _csi_obj_t {
    mp_obj_base_t base;
} csi_obj_t;

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// Set when Android takes the camera away (e.g. the app is no longer
// visible). Cleared by reset().
std::atomic<bool> g_cam_lost{false};

void mark_camera_lost() {
    g_cam_lost.store(true);
    if (g_cam.active_wait_sem) {
        sem_post(g_cam.active_wait_sem);
    }
}

void check_camera_lost() {
    if (g_cam_lost.load()) {
        raise_os_error(MP_EIO, "camera disconnected (app not in foreground)");
    }
}

void on_device_disconnected(void *context, ACameraDevice *device) {
    (void) context;
    (void) device;
    LOGW("camera device disconnected");
    mark_camera_lost();
}

void on_device_error(void *context, ACameraDevice *device, int error) {
    (void) context;
    (void) device;
    LOGW("camera device error: %d", error);
    mark_camera_lost();
}

// Tears down the session/reader/request layer only. The device stays
// open. Idempotent, safe to call when nothing is configured yet. Used
// both by camera_close_all() and by ensure_session() before rebuilding
// after a pixformat()/framesize()/framebuffers() change.
//
// GENERATION/IN-FLIGHT TEARDOWN ORDERING -- this is the actual fix for a
// real race (a straggler on_image_available() callback touching a
// buffer that's about to be freed/reallocated by the next
// framebuffer_resize() call), not just a detail. Must happen in exactly
// this order: (1) stop the repeating request so no NEW capture starts,
// (2) unregister the image listener so no NEW callback can even begin,
// (3) bump the generation so a callback already past step (2) but not
// yet started self-discards on entry, (4) spin until no callback is
// mid-execution (the generation check alone is a race without this --
// a straggler that already passed the check can still be freed out from
// under mid-memcpy). Only after (4) is it safe to know nothing will
// touch the framebuffer or reader concurrently. see session-state.
void close_session_and_reader() {
    if (g_cam.session) {
        ACameraCaptureSession_stopRepeating(g_cam.session);
    }
    if (g_cam.reader) {
        AImageReader_setImageListener(g_cam.reader, nullptr);
    }
    g_cam.generation.fetch_add(1, std::memory_order_acq_rel);
    while (g_cam.callback_in_flight.load(std::memory_order_acquire) != 0) {
        // Busy-spin, no sleep: the callback body is short (one memcpy'd
        // frame), expected sub-microsecond. Not reached at all unless a
        // callback was genuinely in flight at the moment of teardown.
    }

    if (g_cam.session) {
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
        // Also invalidates reader_window. Do NOT ANativeWindow_release()
        // it separately. Owned by the reader (AImageReader_getWindow's own
        // doc comment).
        AImageReader_delete(g_cam.reader);
        g_cam.reader = nullptr;
        g_cam.reader_window = nullptr;
    }
}

// Writes g_ctl into a capture request. Entries a HAL ignores are harmless;
// callers that need to know whether a control exists check the camera
// characteristics first (see csi_brightness() etc).
void apply_controls(ACaptureRequest *req) {
    uint8_t ae_lock = (g_ctl.auto_gain && g_ctl.auto_exposure) ? ACAMERA_CONTROL_AE_LOCK_OFF : ACAMERA_CONTROL_AE_LOCK_ON;
    uint8_t awb_lock = g_ctl.auto_whitebal ? ACAMERA_CONTROL_AWB_LOCK_OFF : ACAMERA_CONTROL_AWB_LOCK_ON;
    ACaptureRequest_setEntry_u8(req, ACAMERA_CONTROL_AE_LOCK, 1, &ae_lock);
    ACaptureRequest_setEntry_u8(req, ACAMERA_CONTROL_AWB_LOCK, 1, &awb_lock);
    ACaptureRequest_setEntry_i32(req, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &g_ctl.ev_index);
    if (g_ctl.fps_max > 0) {
        int32_t range[2] = {g_ctl.fps_min, g_ctl.fps_max};
        ACaptureRequest_setEntry_i32(req, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, range);
    }
    ACaptureRequest_setEntry_u8(req, ACAMERA_CONTROL_EFFECT_MODE, 1, &g_ctl.effect);
    int32_t pattern = g_ctl.colorbar ? ACAMERA_SENSOR_TEST_PATTERN_MODE_COLOR_BARS : ACAMERA_SENSOR_TEST_PATTERN_MODE_OFF;
    ACaptureRequest_setEntry_i32(req, ACAMERA_SENSOR_TEST_PATTERN_MODE, 1, &pattern);
    uint8_t flash = g_ctl.torch ? ACAMERA_FLASH_MODE_TORCH : ACAMERA_FLASH_MODE_OFF;
    ACaptureRequest_setEntry_u8(req, ACAMERA_FLASH_MODE, 1, &flash);
    if (g_ctl.zoom > 0.0f) {
        ACaptureRequest_setEntry_float(req, ACAMERA_CONTROL_ZOOM_RATIO, 1, &g_ctl.zoom);
    }
}

// Pushes changed g_ctl into the running session, if any. Without a
// session, ensure_session() applies g_ctl when it builds the next one.
void push_controls(const char *who) {
    if (!g_cam.session || !g_cam.request) {
        return;
    }
    apply_controls(g_cam.request);
    if (ACameraCaptureSession_setRepeatingRequest(g_cam.session, nullptr, 1, &g_cam.request, nullptr) != ACAMERA_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: failed to apply camera setting", who);
        raise_os_error(MP_EIO, msg);
    }
}

// Camera characteristics of the open camera; caller frees. Raises if
// reset() hasn't run.
ACameraMetadata *get_characteristics(const char *who) {
    if (!g_cam.device) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: camera not reset -- call reset() first", who);
        raise_os_error(MP_EINVAL, msg);
    }
    ACameraMetadata *metadata = nullptr;
    if (ACameraManager_getCameraCharacteristics(g_cam.manager, g_cam.camera_id.c_str(), &metadata) != ACAMERA_OK || !metadata) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: failed to read camera characteristics", who);
        raise_os_error(MP_EIO, msg);
    }
    return metadata;
}

// Context handed to on_image_available() via AImageReader_setImageListener
// -- deliberately leaked (never delete'd) so its own lifetime question
// never has to be answered; the generation check is what makes a stale
// one harmless. AImageReader_ImageListener itself is copied BY VALUE
// into the reader (NDK contract), so only this context pointer, not the
// listener struct, needs to outlive the registration call.
struct FrameContext {
    uint64_t generation;
    int32_t width;
    int32_t height;
};

// Fires on an internal Android thread, NOT the mp-engine-worker thread
// that runs MicroPython bytecode (confirmed: AImageReader's own
// documented threading contract). This is why this function may NEVER
// call m_malloc, raise_os_error()/nlr_raise, mp_handle_pending, or any
// other MicroPython-runtime function -- those are not safe to call
// concurrently with the worker thread's own use of the GC heap / NLR
// machinery. Only plain C memory (malloc/free, memcpy) and the ported
// framebuffer_t/queue_t (lock-free, safe by construction) may be touched
// here. see session-state.
void on_image_available(void *context, AImageReader *reader) {
    auto *ctx = static_cast<FrameContext *>(context);
    g_cam.callback_in_flight.fetch_add(1, std::memory_order_acq_rel);
    struct Guard {
        ~Guard() { g_cam.callback_in_flight.fetch_sub(1, std::memory_order_acq_rel); }
    } guard;

    if (ctx->generation != g_cam.generation.load(std::memory_order_acquire)) {
        return; // stale reader -- see close_session_and_reader()'s own comment
    }

    AImage *image = nullptr;
    // acquireLatestImage, not acquireNextImage: drops any backlog rather
    // than serially processing stale frames, matching the "movie camera,
    // latest frame wins" model this whole redesign is built around.
    if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || image == nullptr) {
        // Rate-limited: this can fire at sensor fps under sustained
        // backpressure/hardware trouble. Leave the previous, still-valid
        // frame in the framebuffer untouched; the next callback retries.
        static int fail_count = 0;
        if (fail_count == 0 || (fail_count % 30) == 0) {
            LOGW("camera: failed to acquire frame in callback (count=%d)", fail_count + 1);
        }
        fail_count++;
        return;
    }

    framebuffer_t *fb = framebuffer_get(FB_MAINFB_ID);
    // PEEK: get a slot to write into without popping it yet.
    // framebuffer_release()'s own FB_FLAG_FREE|FB_FLAG_CHECK_LAST call
    // below does the actual pop -- acquiring without PEEK here first
    // would pop it twice (once here, once inside release()), silently
    // dropping the frame instead of moving it to the used queue. see
    // session-state.
    vbuffer_t *buf = framebuffer_acquire(fb, FB_FLAG_FREE | FB_FLAG_PEEK);
    if (buf == nullptr) {
        // No free slot (shouldn't happen with CHECK_LAST release below,
        // but if it ever does, drop this frame rather than block or crash).
        AImage_delete(image);
        return;
    }

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

    // Tightly packed I420 (Y, then U, then V, pixel stride 1 throughout)
    // -- matches the size camera_module.cpp's ensure_session() passed to
    // framebuffer_resize(). csi_snapshot()'s convert_to_grayscale/
    // convert_to_rgb565 assume this exact layout.
    int32_t w = ctx->width, h = ctx->height;
    int32_t uv_w = (w + 1) / 2, uv_h = (h + 1) / 2;
    size_t y_size = (size_t) w * h;
    size_t uv_size = (size_t) uv_w * uv_h;
    uint8_t *dst_y = buf->data;
    uint8_t *dst_u = buf->data + y_size;
    uint8_t *dst_v = dst_u + uv_size;
    for (int32_t row = 0; row < h; row++) {
        memcpy(dst_y + (size_t) row * w, y_data + (size_t) row * y_row_stride, w);
    }
    for (int32_t row = 0; row < uv_h; row++) {
        const uint8_t *u_row_ptr = u_data + (size_t) row * u_row_stride;
        const uint8_t *v_row_ptr = v_data + (size_t) row * v_row_stride;
        uint8_t *dst_u_row = dst_u + (size_t) row * uv_w;
        uint8_t *dst_v_row = dst_v + (size_t) row * uv_w;
        for (int32_t col = 0; col < uv_w; col++) {
            dst_u_row[col] = u_row_ptr[col * u_pixel_stride];
            dst_v_row[col] = v_row_ptr[col * v_pixel_stride];
        }
    }
    AImage_delete(image);

    // NOW pop free -> push used (or overwrite-in-place if the consumer
    // is behind -- CHECK_LAST's double/triple-buffer fallback).
    framebuffer_release(fb, FB_FLAG_FREE | FB_FLAG_CHECK_LAST);
    g_cam.frame_seq.fetch_add(1, std::memory_order_relaxed);
    if (g_cam.active_wait_sem) {
        sem_post(g_cam.active_wait_sem);
    }
}

// (Re)builds the session/reader/request/framebuffer layer for the
// CURRENT width/height/pixfmt/buf_count. Called lazily from
// csi_snapshot() rather than eagerly from pixformat()/framesize()/
// framebuffers(). A script may call several setters before ever
// capturing a frame, so rebuilding on every setter call would do
// wasted/duplicate work.
void ensure_session() {
    if (g_cam.session) {
        return;
    }
    close_session_and_reader();

    if (!g_cam.persistent_frame_sem_ready) {
        sem_init(&g_cam.persistent_frame_sem, 0, 0);
        g_cam.persistent_frame_sem_ready = true;
        g_cam.active_wait_sem = &g_cam.persistent_frame_sem;
        // framebuffer_init() must run exactly once, before the first-ever
        // framebuffer_resize() call below -- it's what sets fb->dynamic
        // (so resize() actually allocates instead of silently treating a
        // zero-initialized raw_size as "buffer too small, fail"). Lazily
        // guarded here alongside the semaphore init since both are
        // real, one-time, process-lifetime setup, not per-session state.
        framebuffer_init(framebuffer_get(FB_MAINFB_ID), NULL, 0, true, true);
    }

    if (AImageReader_new(g_cam.width, g_cam.height, AIMAGE_FORMAT_YUV_420_888, 2, &g_cam.reader) != AMEDIA_OK) {
        raise_os_error(MP_EIO, "camera: failed to create image reader");
    }
    if (AImageReader_getWindow(g_cam.reader, &g_cam.reader_window) != AMEDIA_OK) {
        raise_os_error(MP_EIO, "camera: failed to get reader window");
    }

    // Tightly-packed I420 frame size -- see on_image_available()'s own
    // comment for the exact layout this must match.
    int32_t uv_w = (g_cam.width + 1) / 2, uv_h = (g_cam.height + 1) / 2;
    size_t frame_size = (size_t) g_cam.width * g_cam.height + 2 * (size_t) uv_w * uv_h;
    framebuffer_t *fb = framebuffer_get(FB_MAINFB_ID);
    if (framebuffer_resize(fb, g_cam.buf_count, frame_size) != 0) {
        raise_os_error(MP_EIO, "camera: failed to allocate framebuffer");
    }

    // Permanent registration, not transient (the old design registered a
    // stack-local listener only while a single snapshot() call was
    // waiting -- the actual bug this whole redesign exists to fix). See
    // FrameContext's own comment for the leaked-pointer reasoning.
    auto *ctx = new FrameContext{g_cam.generation.load(std::memory_order_acquire), g_cam.width, g_cam.height};
    AImageReader_ImageListener listener = {ctx, on_image_available};
    if (AImageReader_setImageListener(g_cam.reader, &listener) != AMEDIA_OK) {
        raise_os_error(MP_EIO, "camera: failed to register frame listener");
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
    apply_controls(g_cam.request);
    if (ACameraCaptureSession_setRepeatingRequest(g_cam.session, nullptr, 1, &g_cam.request, nullptr) != ACAMERA_OK) {
        raise_os_error(MP_EIO, "camera: failed to start capture");
    }
}

// Both operate on the tightly-packed I420 buffer on_image_available()
// writes (see its own comment) -- plain offset arithmetic, no AImage/
// plane-stride handling needed here anymore (that's all done once, in
// the callback, not once per snapshot() call).
void convert_to_grayscale_packed(const uint8_t *src, image_t *out) {
    memcpy(out->data, src, (size_t) out->w * out->h);
}

// BT.601: R = Y + 1.402*Cr, B = Y + 1.772*Cb, G = Y - 0.344*Cb - 0.714*Cr.
// u=Cb, v=Cr, both already minus 128.
// see session-state: camera_module.cpp#convert_to_rgb565
inline uint16_t yuv_to_rgb565(int y, int u, int v) {
    int ry = (179 * v) >> 7;
    int gy = ((44 * u) + (91 * v)) >> 7;
    int by = (227 * u) >> 7;
    int r = __USAT(y + ry, 8);
    int g = __USAT(y - gy, 8);
    int b = __USAT(y + by, 8);
    return COLOR_R8_G8_B8_TO_RGB565(r, g, b);
}

// see session-state: camera_module.cpp#convert_to_rgb565
void convert_to_rgb565_packed(const uint8_t *src, image_t *out) {
    int32_t w = out->w, h = out->h;
    int32_t uv_w = (w + 1) / 2, uv_h = (h + 1) / 2;
    size_t y_size = (size_t) w * h;
    size_t uv_size = (size_t) uv_w * uv_h;
    const uint8_t *y_data = src;
    const uint8_t *u_data = src + y_size;
    const uint8_t *v_data = u_data + uv_size;

    uint16_t *dst = (uint16_t *) out->data;
    for (int32_t row = 0; row < h; row++) {
        int32_t uv_row = row / 2;
        const uint8_t *y_row_ptr = y_data + (size_t) row * w;
        const uint8_t *u_row_ptr = u_data + (size_t) uv_row * uv_w;
        const uint8_t *v_row_ptr = v_data + (size_t) uv_row * uv_w;
        uint16_t *dst_row = dst + (size_t) row * w;

        for (int32_t col = 0; col < w; col++) {
            int y = y_row_ptr[col];
            int u = (int) u_row_ptr[col / 2] - 128;
            int v = (int) v_row_ptr[col / 2] - 128;
            dst_row[col] = yuv_to_rgb565(y, u, v);
        }
    }
}

// Slow path of convert_image(): any of hmirror/vflip/transpose/window set.
// Walks the output image; each output row is a straight line through the
// source frame, so only a start point and a per-pixel step are computed.
void convert_with_geometry(const uint8_t *src, image_t *out) {
    int32_t W = g_cam.width, H = g_cam.height;
    int32_t uv_w = (W + 1) / 2, uv_h = (H + 1) / 2;
    const uint8_t *y_data = src;
    const uint8_t *u_data = src + (size_t) W * H;
    const uint8_t *v_data = u_data + (size_t) uv_w * uv_h;
    bool rgb = out->pixfmt == PIXFORMAT_RGB565;

    // Step through the windowed (pre-transpose) image per output pixel.
    int32_t dnx = g_geo.transpose ? 0 : 1;
    int32_t dny = g_geo.transpose ? 1 : 0;
    int32_t dsx = g_geo.hmirror ? -dnx : dnx;
    int32_t dsy = g_geo.vflip ? -dny : dny;

    for (int32_t oy = 0; oy < out->h; oy++) {
        int32_t fx = g_geo.win_x + (g_geo.transpose ? oy : 0);
        int32_t fy = g_geo.win_y + (g_geo.transpose ? 0 : oy);
        int32_t sx = g_geo.hmirror ? W - 1 - fx : fx;
        int32_t sy = g_geo.vflip ? H - 1 - fy : fy;
        if (rgb) {
            uint16_t *dst = (uint16_t *) out->data + (size_t) oy * out->w;
            for (int32_t ox = 0; ox < out->w; ox++, sx += dsx, sy += dsy) {
                size_t uv = (size_t) (sy / 2) * uv_w + sx / 2;
                dst[ox] = yuv_to_rgb565(y_data[(size_t) sy * W + sx], (int) u_data[uv] - 128, (int) v_data[uv] - 128);
            }
        } else {
            uint8_t *dst = out->data + (size_t) oy * out->w;
            for (int32_t ox = 0; ox < out->w; ox++, sx += dsx, sy += dsy) {
                dst[ox] = y_data[(size_t) sy * W + sx];
            }
        }
    }
}

// see session-state: camera_module.cpp#image_alloc_tf_aligned
constexpr size_t kTfAlignment = 64;

void image_alloc_tf_aligned(image_t *img, size_t size) {
    size_t aligned_size = (size + kTfAlignment - 1) & ~(kTfAlignment - 1);
    img->_raw = (uint8_t *) m_malloc(aligned_size + kTfAlignment - 1);
    img->data = (uint8_t *) (((uintptr_t) img->_raw + kTfAlignment - 1) & ~((uintptr_t) (kTfAlignment - 1)));
    // Permanent, cheap sanity check on the alignment math above. Silent
    // when correct, LOGW's if the invariant this function exists for is
    // ever actually violated.
    if (((uintptr_t) img->data) % kTfAlignment != 0) {
        LOGW("image_alloc_tf_aligned: data=%p is NOT %zu-byte aligned (bug)", img->data, kTfAlignment);
    }
}

// packed_yuv must point at a tightly-packed I420 buffer at g_cam.width x
// g_cam.height (i.e. a framebuffer vbuffer_t's own ->data) -- called from
// csi_snapshot() only, on the worker thread, so m_malloc here is safe
// (see on_image_available()'s own comment for why that's NOT true there).
mp_obj_t convert_image(const uint8_t *packed_yuv) {
    image_t img = {0};
    output_size(&img.w, &img.h);
    img.pixfmt = g_cam.pixfmt;
    image_alloc_tf_aligned(&img, image_size(&img));

    if (g_geo.hmirror || g_geo.vflip || g_geo.transpose || g_geo.windowed) {
        convert_with_geometry(packed_yuv, &img);
    } else if (g_cam.pixfmt == PIXFORMAT_RGB565) {
        convert_to_rgb565_packed(packed_yuv, &img);
    } else {
        convert_to_grayscale_packed(packed_yuv, &img);
    }
    return py_image_from_struct(&img);
}

long now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long) ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

// Blocks (interruptibly) until the framebuffer has at least one captured
// frame -- only reachable right after reset() or a pixformat()/
// framesize()/framebuffers() rebuild, before on_image_available() has
// fired even once. Every subsequent csi_snapshot() call in the session
// takes the fast path below and returns immediately. Reuses the exact
// interruptible-wait shape this file already had (5ms-chunked
// sem_timedwait + mp_handle_pending for Interrupt responsiveness), just
// checking framebuffer_readable() instead of a raw per-call semaphore.
void wait_for_frame() {
    framebuffer_t *fb = framebuffer_get(FB_MAINFB_ID);
    if (framebuffer_readable(fb)) {
        return;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        bool got = false;
        for (long waited = 0; waited < kFirstFrameTimeoutMs; waited += kWaitChunkMs) {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_nsec += kWaitChunkMs * 1000000L;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec += 1;
                deadline.tv_nsec -= 1000000000L;
            }
            sem_timedwait(g_cam.active_wait_sem, &deadline);
            check_camera_lost();
            if (framebuffer_readable(fb)) {
                got = true;
                break;
            }
            // Raises (nlr_jump into the `else` branch below) if the user
            // tapped Interrupt during the wait.
            mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
        }
        if (!got) {
            raise_os_error(MP_ETIMEDOUT, "camera snapshot timed out");
        }
        nlr_pop();
    } else {
        nlr_jump(nlr.ret_val);
    }
}

// see session-state: camera_module.cpp#csi_snapshot_warmup
//
// No longer actively pulls/deletes frames itself -- draining is now
// unconditional and continuous regardless of whether warmup or
// snapshot() is running (that's the whole point of this redesign).
// frames=N waits for g_cam.frame_seq to advance by N, with the
// stuck-camera timeout reset on every observed frame (so a genuinely
// wedged camera still raises ETIMEDOUT, matching this file's original
// per-frame-timeout behavior, rather than a single dead-air timeout for
// the whole batch). time= alone is a plain interruptible sleep with no
// reader interaction at all -- a wedged camera can no longer surface
// EIO/ETIMEDOUT during a pure time= wait, since there's nothing left to
// fail; the real signal moves to the next snapshot() call.
void csi_snapshot_warmup(mp_int_t time_limit_ms, mp_int_t frames_limit) {
    long start = now_ms();
    long last_progress = now_ms();
    unsigned long start_seq = g_cam.frame_seq.load(std::memory_order_relaxed);

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        while (true) {
            if (time_limit_ms >= 0 && (now_ms() - start) >= time_limit_ms) {
                break;
            }
            if (frames_limit >= 0 &&
                (long) (g_cam.frame_seq.load(std::memory_order_relaxed) - start_seq) >= frames_limit) {
                break;
            }

            unsigned long seq_before = g_cam.frame_seq.load(std::memory_order_relaxed);
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_nsec += kWaitChunkMs * 1000000L;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec += 1;
                deadline.tv_nsec -= 1000000000L;
            }
            sem_timedwait(g_cam.active_wait_sem, &deadline);
            check_camera_lost();

            if (g_cam.frame_seq.load(std::memory_order_relaxed) != seq_before) {
                last_progress = now_ms();
            } else if (frames_limit >= 0 && (now_ms() - last_progress) >= kSnapshotTimeoutMs) {
                // Only enforced when frames_limit is actually in play --
                // a pure time= sleep must never fail just because no
                // frame arrived during it, see this function's own
                // comment above.
                raise_os_error(MP_ETIMEDOUT, "camera snapshot timed out");
            }
            mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
        }
        nlr_pop();
    } else {
        nlr_jump(nlr.ret_val);
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
    check_camera_lost();
    ensure_session();

    if (args[ARG_time].u_int >= 0 || args[ARG_frames].u_int >= 0) {
        csi_snapshot_warmup(args[ARG_time].u_int, args[ARG_frames].u_int);
        return mp_const_none;
    }

    // Drain-one-frame: blocks (interruptibly) up to one sensor frame
    // interval if the producer hasn't finished a new frame since the
    // last snapshot() call -- correct and expected, not a bug, see
    // wait_for_frame()'s own comment. A wedged camera surfaces as
    // ETIMEDOUT here on its own; no separate staleness detection needed.
    wait_for_frame();

    framebuffer_t *fb = framebuffer_get(FB_MAINFB_ID);
    vbuffer_t *buf = framebuffer_acquire(fb, FB_FLAG_USED | FB_FLAG_PEEK);
    mp_obj_t result = convert_image(buf->data);
    // NOW pop used -> free, freeing this slot for the producer again.
    framebuffer_release(fb, FB_FLAG_USED);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(csi_snapshot_obj, 1, csi_snapshot);

// see session-state: camera_module.cpp#csi_reset
mp_obj_t csi_reset(mp_obj_t self_in) {
    (void) self_in;
    camera_close_all();
    g_cam_lost.store(false);
    g_cam.pixfmt = PIXFORMAT_GRAYSCALE;
    g_cam.width = kDefaultWidth;
    g_cam.height = kDefaultHeight;
    g_cam.buf_count = kDefaultBufCount;
    g_ctl = CameraControls();
    g_geo = CameraGeometry();

    g_cam.manager = ACameraManager_create();
    if (!g_cam.manager) {
        raise_os_error(MP_EIO, "camera: failed to create manager");
    }

    ACameraIdList *ids = nullptr;
    if (ACameraManager_getCameraIdList(g_cam.manager, &ids) != ACAMERA_OK || !ids || ids->numCameras == 0) {
        raise_os_error(MP_ENODEV, "camera: no camera available");
    }

    // see session-state: camera_module.cpp#camera_id_selection
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

mp_obj_t csi_pixformat(size_t n_args, const mp_obj_t *args) {
    if (n_args == 1) {
        return MP_OBJ_NEW_SMALL_INT(g_cam.pixfmt);
    }
    int fmt = mp_obj_get_int(args[1]);
    if (fmt != PIXFORMAT_GRAYSCALE && fmt != PIXFORMAT_RGB565) {
        mp_raise_ValueError(MP_ERROR_TEXT("unsupported pixformat"));
    }
    if ((pixformat_t) fmt != g_cam.pixfmt) {
        g_cam.pixfmt = (pixformat_t) fmt;
        close_session_and_reader(); // rebuilt lazily by the next snapshot()
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(csi_pixformat_obj, 1, 2, csi_pixformat);

// see session-state: camera_module.cpp#csi_framesize
// No-arg form returns the packed (w << 16) | h, so it compares equal to
// the named constant (csi.QVGA etc) that was set.
mp_obj_t csi_framesize(size_t n_args, const mp_obj_t *args) {
    if (n_args == 1) {
        return MP_OBJ_NEW_SMALL_INT(((mp_int_t) g_cam.width << 16) | g_cam.height);
    }
    mp_obj_t size_in = args[1];
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
        // Named constants (csi.QQVGA etc) are packed as (w << 16) | h.
        // Self-describing, no lookup table.
        int packed = mp_obj_get_int(size_in);
        w = packed >> 16;
        h = packed & 0xFFFF;
    }
    if (w <= 0 || h <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid framesize"));
    }
    g_geo.windowed = false;
    g_geo.win_w = g_geo.win_h = g_geo.win_x = g_geo.win_y = 0;
    if (w != g_cam.width || h != g_cam.height) {
        g_cam.width = w;
        g_cam.height = h;
        close_session_and_reader(); // rebuilt lazily by the next snapshot()
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(csi_framesize_obj, 1, 2, csi_framesize);

mp_obj_t csi_width(mp_obj_t self_in) {
    (void) self_in;
    return MP_OBJ_NEW_SMALL_INT(g_cam.width);
}
static MP_DEFINE_CONST_FUN_OBJ_1(csi_width_obj, csi_width);

mp_obj_t csi_height(mp_obj_t self_in) {
    (void) self_in;
    return MP_OBJ_NEW_SMALL_INT(g_cam.height);
}
static MP_DEFINE_CONST_FUN_OBJ_1(csi_height_obj, csi_height);

// Every YUV_420_888 output size this device's camera actually supports,
// sorted smallest-first by pixel count. Exists because Android camera
// hardware varies per device; there's no compile-time list to hardcode.
// see session-state: camera_module.cpp#csi_framesize
mp_obj_t csi_framesize_list(mp_obj_t self_in) {
    (void) self_in;
    if (!g_cam.device) {
        raise_os_error(MP_EINVAL, "camera not reset -- call reset() first");
    }

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

// see session-state: camera_module.cpp#camera_id_selection
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

// Mirrors real OpenMV's sensor.framebuffers(n) (py_csi_ng.c) exactly:
// no-arg form returns the current count, one-arg form validates >= 1
// and, if actually changing, forces a lazy rebuild (same
// close-then-rebuild-on-next-snapshot() path pixformat()/framesize()
// already use) so the new count takes effect via the same safe
// stop/unregister/bump-generation/spin teardown sequence, not a
// concurrent framebuffer_resize() while frames could still be arriving.
mp_obj_t csi_framebuffers(size_t n_args, const mp_obj_t *args) {
    (void) args[0];
    if (n_args == 1) {
        return mp_obj_new_int((mp_int_t) g_cam.buf_count);
    }
    mp_int_t num = mp_obj_get_int(args[1]);
    if (num < 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("framebuffers count must be >= 1"));
    }
    if ((size_t) num != g_cam.buf_count) {
        g_cam.buf_count = (size_t) num;
        close_session_and_reader(); // rebuilt lazily by the next snapshot()
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(csi_framebuffers_obj, 1, 2, csi_framebuffers);

// Runtime-queryable usage reference for an AI (or human) driving this
// module blind over adb, with no repo access -- see
// AdbExecProvider.kt/adb_help.yaml, which is the first hop of this same
// chain (help -> help('modules') -> import csi; print(csi.help())).
// see session-state: camera_module.cpp#csi_help
const char csi_help_text[] =
    "module: csi (camera)\n"
    "class: csi.CSI(cid=None)\n"
    "  cid: optional int, camera id (see camera_list()); default picks first camera\n"
    "methods:\n"
    "  reset(): open/reopen the camera; call first\n"
    "  pixformat(fmt=None): get/set csi.GRAYSCALE or csi.RGB565; changing closes and lazily reopens the session\n"
    "  framesize(size=None): get/set (w,h) tuple or a named constant (csi.QVGA etc); changing closes and lazily reopens\n"
    "  framesize_list(): list of (w,h) this camera actually supports\n"
    "  camera_list(): list of (id, facing, has_flash) for every camera on this device\n"
    "  snapshot(time=None, frames=None):\n"
    "    no args: returns the latest available frame (an image_t); blocks up to one frame interval if none is ready yet; OSError(ETIMEDOUT) if the camera is wedged\n"
    "    time=ms: sleep ms, then return None (warmup; does not raise on a wedged camera)\n"
    "    frames=n: wait for n frames to have arrived, then return None (warmup)\n"
    "  framebuffers(n=None): get/set buffer count (1/2/3 = single/double/triple buffering); default 2\n"
    "  width()/height(): current frame dimensions\n"
    "  auto_gain(enable)/auto_exposure(enable)/auto_whitebal(enable): on/off only; gain_db/exposure_us/rgb_gain_db kwargs accepted but ignored (printed warning)\n"
    "  brightness(level): exposure compensation in EV steps; True if in range, False if not\n"
    "  framerate(fps=None): get/set target fps; set raises ValueError listing supported ranges if fps is not one of them\n"
    "  special_effect(csi.NORMAL|csi.NEGATIVE): False if unsupported\n"
    "  colorbar(enable): sensor test pattern; False if unsupported\n"
    "  hmirror(enable=None)/vflip(enable=None)/transpose(enable=None): get/set, applied in software (not hardware)\n"
    "  window(roi=None): get/set (w,h) centered or (x,y,w,h); clipped to frame; software crop\n"
    "  sleep(enable): True stops streaming (session closes); next snapshot() restarts it\n"
    "  shutdown(enable): True closes the camera entirely; reset() reopens\n"
    "  contrast()/saturation()/quality()/gainceiling()/auto_blc(): accepted, no Camera2 equivalent, always no-ops\n"
    "constants: csi.GRAYSCALE, csi.RGB565, named framesizes (csi.QVGA, csi.VGA, csi.HD, ...), csi.NORMAL, csi.NEGATIVE\n"
    "errors:\n"
    "  OSError(EINVAL): called before reset()\n"
    "  OSError(EIO): camera open/characteristics read failure, or camera disconnected (app not in foreground); call reset() after returning to the app\n"
    "  OSError(EACCES): camera permission not granted\n"
    "  OSError(ENODEV): no camera on this device\n"
    "  OSError(ETIMEDOUT): snapshot() timed out waiting for a frame\n"
    "deviations_from_openmv:\n"
    "  - snapshot() reads from a continuously-running background capture, not an on-demand hardware trigger; it can block up to one frame interval on every call, not just the first\n"
    "  - hmirror/vflip/transpose/window are done in software here, not hardware\n"
    "  - manual gain/exposure/white-balance values are not implemented, only auto on/off\n"
    "see_also: display.help(), android.help()\n"
;
mp_obj_t csi_help() {
    return mp_obj_new_str(csi_help_text, strlen(csi_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(csi_help_obj, csi_help);

// ---------------------------------------------------------------------------
// Camera controls, OpenMV csi names and signatures (py_csi_ng.c), mapped to
// Camera2 capture request entries. Controls without a Camera2 equivalent
// are accepted and do nothing, returning False where OpenMV returns a
// success bool. see g_ctl / apply_controls().

// Manual gain/exposure/white balance values need capture-result read-back,
// not done yet: only the auto on/off part is implemented.
void warn_ignored(mp_obj_t value, const char *name) {
    if (value != mp_const_none) {
        mp_printf(&mp_plat_print, "csi: %s not supported, ignored\n", name);
    }
}

// auto_gain(False) and auto_exposure(False) both map to AE lock: Camera2
// has one auto-exposure loop for gain and exposure time together.
mp_obj_t csi_auto_gain(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_enable, ARG_gain_db, ARG_gain_db_ceiling };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_enable, MP_ARG_BOOL | MP_ARG_REQUIRED, {.u_bool = true}},
        {MP_QSTR_gain_db, MP_ARG_OBJ | MP_ARG_KW_ONLY, {.u_rom_obj = MP_ROM_NONE}},
        {MP_QSTR_gain_db_ceiling, MP_ARG_OBJ | MP_ARG_KW_ONLY, {.u_rom_obj = MP_ROM_NONE}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    warn_ignored(args[ARG_gain_db].u_obj, "gain_db");
    warn_ignored(args[ARG_gain_db_ceiling].u_obj, "gain_db_ceiling");
    g_ctl.auto_gain = args[ARG_enable].u_bool;
    push_controls("auto_gain");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(csi_auto_gain_obj, 2, csi_auto_gain);

mp_obj_t csi_auto_exposure(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_enable, ARG_exposure_us };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_enable, MP_ARG_BOOL | MP_ARG_REQUIRED, {.u_bool = true}},
        {MP_QSTR_exposure_us, MP_ARG_INT | MP_ARG_KW_ONLY, {.u_int = -1}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    if (args[ARG_exposure_us].u_int >= 0) {
        mp_printf(&mp_plat_print, "csi: exposure_us not supported, ignored\n");
    }
    g_ctl.auto_exposure = args[ARG_enable].u_bool;
    push_controls("auto_exposure");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(csi_auto_exposure_obj, 2, csi_auto_exposure);

mp_obj_t csi_auto_whitebal(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_enable, ARG_rgb_gain_db };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_enable, MP_ARG_BOOL | MP_ARG_REQUIRED, {.u_bool = true}},
        {MP_QSTR_rgb_gain_db, MP_ARG_OBJ | MP_ARG_KW_ONLY, {.u_rom_obj = MP_ROM_NONE}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    warn_ignored(args[ARG_rgb_gain_db].u_obj, "rgb_gain_db");
    g_ctl.auto_whitebal = args[ARG_enable].u_bool;
    push_controls("auto_whitebal");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(csi_auto_whitebal_obj, 2, csi_auto_whitebal);

// brightness(level): exposure compensation, one level = 1 EV. False if the
// level is outside the camera's compensation range, like OpenMV's sensors.
mp_obj_t csi_brightness(mp_obj_t self_in, mp_obj_t level_in) {
    (void) self_in;
    mp_int_t level = mp_obj_get_int(level_in);
    ACameraMetadata *metadata = get_characteristics("brightness");
    ACameraMetadata_const_entry range = {}, step = {};
    bool ok = ACameraMetadata_getConstEntry(metadata, ACAMERA_CONTROL_AE_COMPENSATION_RANGE, &range) == ACAMERA_OK && range.count >= 2 &&
        ACameraMetadata_getConstEntry(metadata, ACAMERA_CONTROL_AE_COMPENSATION_STEP, &step) == ACAMERA_OK && step.count >= 1 &&
        step.data.r[0].numerator > 0 && step.data.r[0].denominator > 0;
    int32_t index = 0;
    if (ok) {
        // steps per EV = denominator / numerator (step is e.g. 1/3 EV)
        index = (int32_t) lround((double) level * step.data.r[0].denominator / step.data.r[0].numerator);
        ok = index >= range.data.i32[0] && index <= range.data.i32[1];
    }
    ACameraMetadata_free(metadata);
    if (!ok) {
        return mp_const_false;
    }
    g_ctl.ev_index = index;
    push_controls("brightness");
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_2(csi_brightness_obj, csi_brightness);

// framerate(fps): picks the camera's AE target fps range with max == fps,
// preferring a fixed [fps, fps] range. ValueError if there is none; the
// ranges are listed in the message. No-arg form returns the set value.
mp_obj_t csi_framerate(size_t n_args, const mp_obj_t *args) {
    if (n_args == 1) {
        if (g_ctl.fps_max == 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("framerate not set"));
        }
        return MP_OBJ_NEW_SMALL_INT(g_ctl.fps_max);
    }
    mp_int_t fps = mp_obj_get_int(args[1]);
    ACameraMetadata *metadata = get_characteristics("framerate");
    ACameraMetadata_const_entry entry = {};
    int32_t best_min = 0;
    char ranges[160] = "";
    if (ACameraMetadata_getConstEntry(metadata, ACAMERA_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, &entry) == ACAMERA_OK) {
        for (uint32_t i = 0; i + 1 < entry.count; i += 2) {
            int32_t lo = entry.data.i32[i], hi = entry.data.i32[i + 1];
            size_t used = strlen(ranges);
            snprintf(ranges + used, sizeof(ranges) - used, " [%d,%d]", (int) lo, (int) hi);
            if (hi == fps && lo > best_min) {
                best_min = lo;
            }
        }
    }
    ACameraMetadata_free(metadata);
    if (best_min == 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("framerate %d not supported, ranges:%s"), (int) fps, ranges);
    }
    g_ctl.fps_min = best_min;
    g_ctl.fps_max = (int32_t) fps;
    push_controls("framerate");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(csi_framerate_obj, 1, 2, csi_framerate);

// Returns whether the camera lists u8 value `mode` in characteristic `tag`.
bool has_u8_mode(uint32_t tag, uint8_t mode, const char *who) {
    ACameraMetadata *metadata = get_characteristics(who);
    ACameraMetadata_const_entry entry = {};
    bool found = false;
    if (ACameraMetadata_getConstEntry(metadata, tag, &entry) == ACAMERA_OK) {
        for (uint32_t i = 0; i < entry.count; i++) {
            found |= entry.data.u8[i] == mode;
        }
    }
    ACameraMetadata_free(metadata);
    return found;
}

// special_effect(csi.NORMAL | csi.NEGATIVE). False if the camera lacks it.
mp_obj_t csi_special_effect(mp_obj_t self_in, mp_obj_t sde_in) {
    (void) self_in;
    mp_int_t sde = mp_obj_get_int(sde_in);
    uint8_t mode;
    if (sde == kSdeNormal) {
        mode = ACAMERA_CONTROL_EFFECT_MODE_OFF;
    } else if (sde == kSdeNegative) {
        mode = ACAMERA_CONTROL_EFFECT_MODE_NEGATIVE;
    } else {
        return mp_const_false;
    }
    if (mode != ACAMERA_CONTROL_EFFECT_MODE_OFF && !has_u8_mode(ACAMERA_CONTROL_AVAILABLE_EFFECTS, mode, "special_effect")) {
        return mp_const_false;
    }
    g_ctl.effect = mode;
    push_controls("special_effect");
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_2(csi_special_effect_obj, csi_special_effect);

// colorbar(True): sensor test pattern. False if the camera lacks it.
mp_obj_t csi_colorbar(mp_obj_t self_in, mp_obj_t enable_in) {
    (void) self_in;
    bool enable = mp_obj_is_true(enable_in);
    if (enable) {
        ACameraMetadata *metadata = get_characteristics("colorbar");
        ACameraMetadata_const_entry entry = {};
        bool found = false;
        if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_AVAILABLE_TEST_PATTERN_MODES, &entry) == ACAMERA_OK) {
            for (uint32_t i = 0; i < entry.count; i++) {
                found |= entry.data.i32[i] == ACAMERA_SENSOR_TEST_PATTERN_MODE_COLOR_BARS;
            }
        }
        ACameraMetadata_free(metadata);
        if (!found) {
            return mp_const_false;
        }
    }
    g_ctl.colorbar = enable;
    push_controls("colorbar");
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_2(csi_colorbar_obj, csi_colorbar);

// hmirror()/vflip()/transpose(): no-arg form returns the setting. Applied
// in software, see g_geo.
mp_obj_t geometry_flag(bool *flag, size_t n_args, const mp_obj_t *args) {
    if (n_args == 1) {
        return mp_obj_new_bool(*flag);
    }
    *flag = mp_obj_is_true(args[1]);
    return mp_const_none;
}

mp_obj_t csi_hmirror(size_t n_args, const mp_obj_t *args) {
    return geometry_flag(&g_geo.hmirror, n_args, args);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(csi_hmirror_obj, 1, 2, csi_hmirror);

mp_obj_t csi_vflip(size_t n_args, const mp_obj_t *args) {
    return geometry_flag(&g_geo.vflip, n_args, args);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(csi_vflip_obj, 1, 2, csi_vflip);

mp_obj_t csi_transpose(size_t n_args, const mp_obj_t *args) {
    return geometry_flag(&g_geo.transpose, n_args, args);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(csi_transpose_obj, 1, 2, csi_transpose);

// window((w, h)) centered, or window((x, y, w, h)); clipped to the frame,
// like OpenMV. No-arg form returns (x, y, w, h).
mp_obj_t csi_window(size_t n_args, const mp_obj_t *args) {
    int32_t W = g_cam.width, H = g_cam.height;
    if (n_args == 1) {
        mp_obj_t t[4];
        if (g_geo.windowed) {
            t[0] = MP_OBJ_NEW_SMALL_INT(g_geo.win_x);
            t[1] = MP_OBJ_NEW_SMALL_INT(g_geo.win_y);
            t[2] = MP_OBJ_NEW_SMALL_INT(g_geo.win_w);
            t[3] = MP_OBJ_NEW_SMALL_INT(g_geo.win_h);
        } else {
            t[0] = t[1] = MP_OBJ_NEW_SMALL_INT(0);
            t[2] = MP_OBJ_NEW_SMALL_INT(W);
            t[3] = MP_OBJ_NEW_SMALL_INT(H);
        }
        return mp_obj_new_tuple(4, t);
    }
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(args[1], &len, &items);
    int32_t x, y, w, h;
    if (len == 2) {
        w = mp_obj_get_int(items[0]);
        h = mp_obj_get_int(items[1]);
        x = W / 2 - w / 2;
        y = H / 2 - h / 2;
    } else if (len == 4) {
        x = mp_obj_get_int(items[0]);
        y = mp_obj_get_int(items[1]);
        w = mp_obj_get_int(items[2]);
        h = mp_obj_get_int(items[3]);
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Expected (w, h) or (x, y, w, h) tuple/list."));
    }
    if (w < 1 || h < 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid ROI dimensions!"));
    }
    int32_t x0 = std::max(x, (int32_t) 0), y0 = std::max(y, (int32_t) 0);
    int32_t x1 = std::min(x + w, W), y1 = std::min(y + h, H);
    if (x1 <= x0 || y1 <= y0) {
        mp_raise_ValueError(MP_ERROR_TEXT("ROI does not overlap on the image!"));
    }
    g_geo.windowed = true;
    g_geo.win_x = x0;
    g_geo.win_y = y0;
    g_geo.win_w = x1 - x0;
    g_geo.win_h = y1 - y0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(csi_window_obj, 1, 2, csi_window);

// No Camera2 equivalent: accepted, not applied.
mp_obj_t csi_unsupported_bool(mp_obj_t self_in, mp_obj_t value_in) {
    (void) self_in;
    (void) value_in;
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_2(csi_unsupported_bool_obj, csi_unsupported_bool);

mp_obj_t csi_unsupported_none(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    (void) n_args;
    (void) pos_args;
    (void) kw_args;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(csi_unsupported_none_obj, 2, csi_unsupported_none);

// sleep(True) stops streaming; the next snapshot() restarts it.
mp_obj_t csi_sleep(mp_obj_t self_in, mp_obj_t enable_in) {
    (void) self_in;
    if (mp_obj_is_true(enable_in)) {
        close_session_and_reader();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(csi_sleep_obj, csi_sleep);

// shutdown(True) closes the camera; reset() reopens it.
mp_obj_t csi_shutdown(mp_obj_t self_in, mp_obj_t enable_in) {
    (void) self_in;
    if (mp_obj_is_true(enable_in)) {
        camera_close_all();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(csi_shutdown_obj, csi_shutdown);

// see session-state: camera_module.cpp#camera_id_selection
mp_obj_t csi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_cid };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_cid, MP_ARG_INT | MP_ARG_KW_ONLY, {.u_int = -1}},
    };
    // make_new()'s own calling convention needs mp_arg_parse_all_kw_array
    // specifically. Same call shape display_make_new() uses.
    mp_arg_val_t kw_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, kw_args);
    g_cam.requested_cid = kw_args[ARG_cid].u_int;

    csi_obj_t *self = m_new_obj(csi_obj_t);
    self->base.type = type;
    return MP_OBJ_FROM_PTR(self);
}

// Keep csi_help_text (csi.help(), above) in sync with this table -- see
// DEVELOPER.md's "adb server" section.
const mp_rom_map_elem_t csi_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&csi_reset_obj)},
    {MP_ROM_QSTR(MP_QSTR_pixformat), MP_ROM_PTR(&csi_pixformat_obj)},
    {MP_ROM_QSTR(MP_QSTR_framesize), MP_ROM_PTR(&csi_framesize_obj)},
    {MP_ROM_QSTR(MP_QSTR_framesize_list), MP_ROM_PTR(&csi_framesize_list_obj)},
    {MP_ROM_QSTR(MP_QSTR_camera_list), MP_ROM_PTR(&csi_camera_list_obj)},
    {MP_ROM_QSTR(MP_QSTR_snapshot), MP_ROM_PTR(&csi_snapshot_obj)},
    {MP_ROM_QSTR(MP_QSTR_framebuffers), MP_ROM_PTR(&csi_framebuffers_obj)},
    {MP_ROM_QSTR(MP_QSTR_width), MP_ROM_PTR(&csi_width_obj)},
    {MP_ROM_QSTR(MP_QSTR_height), MP_ROM_PTR(&csi_height_obj)},
    {MP_ROM_QSTR(MP_QSTR_auto_gain), MP_ROM_PTR(&csi_auto_gain_obj)},
    {MP_ROM_QSTR(MP_QSTR_auto_exposure), MP_ROM_PTR(&csi_auto_exposure_obj)},
    {MP_ROM_QSTR(MP_QSTR_auto_whitebal), MP_ROM_PTR(&csi_auto_whitebal_obj)},
    {MP_ROM_QSTR(MP_QSTR_brightness), MP_ROM_PTR(&csi_brightness_obj)},
    {MP_ROM_QSTR(MP_QSTR_framerate), MP_ROM_PTR(&csi_framerate_obj)},
    {MP_ROM_QSTR(MP_QSTR_special_effect), MP_ROM_PTR(&csi_special_effect_obj)},
    {MP_ROM_QSTR(MP_QSTR_colorbar), MP_ROM_PTR(&csi_colorbar_obj)},
    {MP_ROM_QSTR(MP_QSTR_contrast), MP_ROM_PTR(&csi_unsupported_bool_obj)},
    {MP_ROM_QSTR(MP_QSTR_saturation), MP_ROM_PTR(&csi_unsupported_bool_obj)},
    {MP_ROM_QSTR(MP_QSTR_quality), MP_ROM_PTR(&csi_unsupported_bool_obj)},
    {MP_ROM_QSTR(MP_QSTR_gainceiling), MP_ROM_PTR(&csi_unsupported_none_obj)},
    {MP_ROM_QSTR(MP_QSTR_auto_blc), MP_ROM_PTR(&csi_unsupported_none_obj)},
    {MP_ROM_QSTR(MP_QSTR_sleep), MP_ROM_PTR(&csi_sleep_obj)},
    {MP_ROM_QSTR(MP_QSTR_shutdown), MP_ROM_PTR(&csi_shutdown_obj)},
    {MP_ROM_QSTR(MP_QSTR_hmirror), MP_ROM_PTR(&csi_hmirror_obj)},
    {MP_ROM_QSTR(MP_QSTR_vflip), MP_ROM_PTR(&csi_vflip_obj)},
    {MP_ROM_QSTR(MP_QSTR_transpose), MP_ROM_PTR(&csi_transpose_obj)},
    {MP_ROM_QSTR(MP_QSTR_window), MP_ROM_PTR(&csi_window_obj)},
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
    {MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&csi_help_obj)},
    {MP_ROM_QSTR(MP_QSTR_GRAYSCALE), MP_ROM_INT(PIXFORMAT_GRAYSCALE)},
    {MP_ROM_QSTR(MP_QSTR_RGB565), MP_ROM_INT(PIXFORMAT_RGB565)},
    // OpenMV framesize names; the camera must list the size (framesize_list()).
    {MP_ROM_QSTR(MP_QSTR_QCIF), MP_ROM_INT(FRAMESIZE_PACK(176, 144))},
    {MP_ROM_QSTR(MP_QSTR_CIF), MP_ROM_INT(FRAMESIZE_PACK(352, 288))},
    {MP_ROM_QSTR(MP_QSTR_QSIF), MP_ROM_INT(FRAMESIZE_PACK(176, 120))},
    {MP_ROM_QSTR(MP_QSTR_SIF), MP_ROM_INT(FRAMESIZE_PACK(352, 240))},
    {MP_ROM_QSTR(MP_QSTR_QQQVGA), MP_ROM_INT(FRAMESIZE_PACK(80, 60))},
    {MP_ROM_QSTR(MP_QSTR_QQVGA), MP_ROM_INT(FRAMESIZE_PACK(160, 120))},
    {MP_ROM_QSTR(MP_QSTR_QVGA), MP_ROM_INT(FRAMESIZE_PACK(320, 240))},
    {MP_ROM_QSTR(MP_QSTR_VGA), MP_ROM_INT(FRAMESIZE_PACK(640, 480))},
    {MP_ROM_QSTR(MP_QSTR_HQVGA), MP_ROM_INT(FRAMESIZE_PACK(240, 160))},
    {MP_ROM_QSTR(MP_QSTR_HVGA), MP_ROM_INT(FRAMESIZE_PACK(480, 320))},
    {MP_ROM_QSTR(MP_QSTR_WVGA), MP_ROM_INT(FRAMESIZE_PACK(720, 480))},
    {MP_ROM_QSTR(MP_QSTR_WVGA2), MP_ROM_INT(FRAMESIZE_PACK(752, 480))},
    {MP_ROM_QSTR(MP_QSTR_SVGA), MP_ROM_INT(FRAMESIZE_PACK(800, 600))},
    {MP_ROM_QSTR(MP_QSTR_XGA), MP_ROM_INT(FRAMESIZE_PACK(1024, 768))},
    {MP_ROM_QSTR(MP_QSTR_WXGA), MP_ROM_INT(FRAMESIZE_PACK(1280, 768))},
    {MP_ROM_QSTR(MP_QSTR_SXGA), MP_ROM_INT(FRAMESIZE_PACK(1280, 1024))},
    {MP_ROM_QSTR(MP_QSTR_SXGAM), MP_ROM_INT(FRAMESIZE_PACK(1280, 960))},
    {MP_ROM_QSTR(MP_QSTR_UXGA), MP_ROM_INT(FRAMESIZE_PACK(1600, 1200))},
    {MP_ROM_QSTR(MP_QSTR_HD), MP_ROM_INT(FRAMESIZE_PACK(1280, 720))},
    {MP_ROM_QSTR(MP_QSTR_FHD), MP_ROM_INT(FRAMESIZE_PACK(1920, 1080))},
    {MP_ROM_QSTR(MP_QSTR_QHD), MP_ROM_INT(FRAMESIZE_PACK(2560, 1440))},
    {MP_ROM_QSTR(MP_QSTR_QXGA), MP_ROM_INT(FRAMESIZE_PACK(2048, 1536))},
    {MP_ROM_QSTR(MP_QSTR_WQXGA), MP_ROM_INT(FRAMESIZE_PACK(2560, 1600))},
    {MP_ROM_QSTR(MP_QSTR_WQXGA2), MP_ROM_INT(FRAMESIZE_PACK(2592, 1944))},
    {MP_ROM_QSTR(MP_QSTR_NORMAL), MP_ROM_INT(kSdeNormal)},
    {MP_ROM_QSTR(MP_QSTR_NEGATIVE), MP_ROM_INT(kSdeNegative)},
};
MP_DEFINE_CONST_DICT(csi_module_globals, csi_module_globals_table);

} // namespace

// android.light.on()/off(): torch, tied to the active csi capture
// session rather than a standalone flashlight.
// see session-state: camera_module.cpp#android_light_torch
//
// Declared outside the anonymous namespace (extern prefix required, C++
// gives a const global internal linkage by default) so android_module.cpp
// can reference the function objects.
mp_obj_t android_light_set(bool on) {
    if (!g_cam.device) {
        raise_os_error(MP_EINVAL, "android.light: camera not reset -- call csi.CSI().reset() first");
    }
    ensure_session();

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

    g_ctl.torch = on;
    push_controls("android.light");
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

// android.zoom.set(ratio)/range().
// see session-state: camera_module.cpp#android_zoom
mp_obj_t android_zoom_set(mp_obj_t ratio_in) {
    if (!g_cam.device) {
        raise_os_error(MP_EINVAL, "android.zoom: camera not reset -- call csi.CSI().reset() first");
    }
    ensure_session();
    g_ctl.zoom = (float) mp_obj_get_float(ratio_in);
    push_controls("android.zoom");
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

// extern "C", not inside the anonymous namespace above: genhdr/
// moduledefs.h declares `extern const struct _mp_obj_module_t csi_module;`
// with C linkage, since MicroPython's own module machinery is plain C.
// An anonymous-namespace definition would have internal linkage and the
// real declaration could never bind to it. A link error only, not a
// compile error.
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

extern "C" void camera_get_current_size(int32_t *width, int32_t *height, bool *color) {
    output_size(width, height);
    *color = (g_cam.pixfmt == PIXFORMAT_RGB565);
}
