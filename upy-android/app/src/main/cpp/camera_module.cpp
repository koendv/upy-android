// upy-android camera module: CameraX ImageAnalysis frames as
// image.Image, through camera_jni_bridge.cpp and CameraShim.kt.
// One camera open at a time. Frames are upright as the phone was held
// when the camera opened; the rotation is done here, while copying.

#include "camera_module.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"
#include "imlib.h"
#include "py_image.h"
}

#include "camera_jni_bridge.h"

namespace {

// snapshot() checks for Interrupt this often: twice the frame period at
// 25 fps, so at normal frame rates a frame comes first.
constexpr long kPollMs = 80;
// No frame for this long: OSError(ETIMEDOUT).
constexpr long kFrameTimeoutMs = 2000;
constexpr long kFirstFrameTimeoutMs = 5000;
// A camera control CameraX has not confirmed by then: OSError(ETIMEDOUT).
constexpr long kControlTimeoutMs = 5000;

constexpr int32_t kDefaultWidth = 320;
constexpr int32_t kDefaultHeight = 240;

// TFLite/LiteRT want 64-byte aligned input.
constexpr size_t kTfAlignment = 64;

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// msg is malloc'd by camera_jni_bridge.cpp.
void raise_os_error_free(int errno_, char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    free(msg);
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

long now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long) ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

struct camera_obj_t {
    mp_obj_base_t base;
    bool open;
    int32_t out_w, out_h;  // upright, as snapshot() returns
    int32_t src_w, src_h;  // sensor orientation, as CameraX delivers
    int32_t rotation;      // clockwise degrees from src to out
    pixformat_t pixfmt;
    int64_t last_seq;      // 0: no frame returned yet
    uint8_t *slots[CAMERA_BRIDGE_SLOTS];
};

camera_obj_t *g_open_camera = nullptr;

void raise_if_closed(camera_obj_t *self) {
    if (!self->open) {
        raise_os_error(MP_EINVAL, "camera: closed -- open a new camera.Camera()");
    }
}

void image_alloc_tf_aligned(image_t *img, size_t size) {
    size_t aligned_size = (size + kTfAlignment - 1) & ~(kTfAlignment - 1);
    img->_raw = (uint8_t *) m_malloc(aligned_size + kTfAlignment - 1);
    img->data = (uint8_t *) (((uintptr_t) img->_raw + kTfAlignment - 1) & ~((uintptr_t) (kTfAlignment - 1)));
}

// Rotation clockwise by rot (0/90/180/270) from a W x H frame: each
// output row is a straight line through the source, so a start index
// and a step per pixel are enough.
ptrdiff_t src_start(int32_t rot, int32_t W, int32_t H, int32_t oy) {
    switch (rot) {
        case 90: return (ptrdiff_t) (H - 1) * W + oy;
        case 180: return (ptrdiff_t) (H - 1 - oy) * W + (W - 1);
        case 270: return (ptrdiff_t) (W - 1 - oy);
        default: return (ptrdiff_t) oy * W;
    }
}

ptrdiff_t src_step(int32_t rot, int32_t W) {
    return rot == 90 ? -W : rot == 180 ? -1 : rot == 270 ? W : 1;
}

void rotate_gray(const uint8_t *src, int32_t W, int32_t H, int32_t rot, uint8_t *dst) {
    if (rot == 0) {
        memcpy(dst, src, (size_t) W * H);
        return;
    }
    int32_t out_w = rot == 180 ? W : H;
    int32_t out_h = rot == 180 ? H : W;
    ptrdiff_t step = src_step(rot, W);
    for (int32_t oy = 0; oy < out_h; oy++) {
        ptrdiff_t i = src_start(rot, W, H, oy);
        uint8_t *row = dst + (size_t) oy * out_w;
        for (int32_t ox = 0; ox < out_w; ox++, i += step) {
            row[ox] = src[i];
        }
    }
}

// CameraX's RGBA (4 bytes per pixel) to RGB565, rotated.
void rotate_rgba_to_rgb565(const uint8_t *src, int32_t W, int32_t H, int32_t rot, uint16_t *dst) {
    int32_t out_w = rot % 180 == 0 ? W : H;
    int32_t out_h = rot % 180 == 0 ? H : W;
    ptrdiff_t step = src_step(rot, W);
    for (int32_t oy = 0; oy < out_h; oy++) {
        ptrdiff_t i = src_start(rot, W, H, oy);
        uint16_t *row = dst + (size_t) oy * out_w;
        for (int32_t ox = 0; ox < out_w; ox++, i += step) {
            const uint8_t *p = src + 4 * i;
            row[ox] = COLOR_R8_G8_B8_TO_RGB565(p[0], p[1], p[2]);
        }
    }
}

void close_impl(camera_obj_t *self) {
    if (!self->open) {
        return;
    }
    self->open = false;
    for (auto &slot : self->slots) {
        slot = nullptr;
    }
    if (g_open_camera == self) {
        g_open_camera = nullptr;
    }
    char *err = nullptr;
    if (!camera_bridge_close(&err)) {
        free(err);  // best effort, as in mqtt_close_all()
    }
}

// camera.list(): [{"id": str, "facing": str, "sizes": [(w, h), ...]}, ...]
mp_obj_t camera_list() {
    char *err = nullptr;
    int count = 0;
    if (!camera_bridge_count(&count, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    mp_obj_t result = mp_obj_new_list(0, nullptr);
    for (int i = 0; i < count; i++) {
        char *id = nullptr;
        char *facing = nullptr;
        int32_t *sizes = nullptr;
        size_t n_sizes = 0;
        if (!camera_bridge_id(i, &id, &err)) {
            raise_os_error_free(MP_EIO, err);
        }
        mp_obj_t id_obj = mp_obj_new_str(id, strlen(id));
        free(id);
        if (!camera_bridge_facing(i, &facing, &err)) {
            raise_os_error_free(MP_EIO, err);
        }
        mp_obj_t facing_obj = mp_obj_new_str(facing, strlen(facing));
        free(facing);
        if (!camera_bridge_sizes(i, &sizes, &n_sizes, &err)) {
            raise_os_error_free(MP_EIO, err);
        }
        mp_obj_t size_list = mp_obj_new_list(n_sizes, nullptr);
        auto *list = (mp_obj_list_t *) MP_OBJ_TO_PTR(size_list);
        for (size_t j = 0; j < n_sizes; j++) {
            mp_obj_t wh[2] = {MP_OBJ_NEW_SMALL_INT(sizes[2 * j]), MP_OBJ_NEW_SMALL_INT(sizes[2 * j + 1])};
            list->items[j] = mp_obj_new_tuple(2, wh);
        }
        free(sizes);
        mp_obj_t entry = mp_obj_new_dict(3);
        mp_obj_dict_store(entry, MP_OBJ_NEW_QSTR(MP_QSTR_id), id_obj);
        mp_obj_dict_store(entry, MP_OBJ_NEW_QSTR(MP_QSTR_facing), facing_obj);
        mp_obj_dict_store(entry, MP_OBJ_NEW_QSTR(MP_QSTR_sizes), size_list);
        mp_obj_list_append(result, entry);
    }
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_list_obj, camera_list);

mp_obj_t camera_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_id, ARG_size, ARG_format, ARG_frame_rate };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_id, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        {MP_QSTR_size, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        {MP_QSTR_format, MP_ARG_INT, {.u_int = PIXFORMAT_GRAYSCALE}},
        {MP_QSTR_frame_rate, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    const char *id = parsed[ARG_id].u_obj == mp_const_none ? nullptr : mp_obj_str_get_str(parsed[ARG_id].u_obj);
    int32_t want_w = kDefaultWidth, want_h = kDefaultHeight;
    if (parsed[ARG_size].u_obj != mp_const_none) {
        mp_obj_t *wh;
        mp_obj_get_array_fixed_n(parsed[ARG_size].u_obj, 2, &wh);
        want_w = mp_obj_get_int(wh[0]);
        want_h = mp_obj_get_int(wh[1]);
        if (want_w <= 0 || want_h <= 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("camera: size must be (width, height), both > 0"));
        }
    }
    // frame_rate: None (automatic) or (min, max) from
    // supported_frame_rate_ranges().
    int fps_min = 0, fps_max = 0;
    if (parsed[ARG_frame_rate].u_obj != mp_const_none) {
        mp_obj_t *range;
        mp_obj_get_array_fixed_n(parsed[ARG_frame_rate].u_obj, 2, &range);
        fps_min = mp_obj_get_int(range[0]);
        fps_max = mp_obj_get_int(range[1]);
        if (fps_min <= 0 || fps_max < fps_min) {
            mp_raise_ValueError(MP_ERROR_TEXT("camera: frame_rate must be (min, max), 0 < min <= max"));
        }
    }
    mp_int_t format = parsed[ARG_format].u_int;
    if (format != PIXFORMAT_GRAYSCALE && format != PIXFORMAT_RGB565) {
        mp_raise_ValueError(MP_ERROR_TEXT("camera: format must be camera.GRAYSCALE or camera.RGB565"));
    }
    // One camera at a time: a new one takes over.
    if (g_open_camera) {
        close_impl(g_open_camera);
    }

    // Allocated before opening, so a MemoryError cannot leave the camera
    // open without an object to close it.
    auto *self = mp_obj_malloc_with_finaliser(camera_obj_t, type);
    self->open = false;
    self->pixfmt = (pixformat_t) format;
    self->last_seq = 0;
    for (auto &slot : self->slots) {
        slot = nullptr;
    }

    char *err = nullptr;
    int status = 0;
    if (!camera_bridge_open(id, want_w, want_h, format == PIXFORMAT_RGB565, fps_min, fps_max, &status, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    if (status == CAMERA_BRIDGE_NO_PERMISSION) {
        raise_os_error(MP_EACCES, "camera: no camera permission -- allow it in the prompt or in Android Settings, then try again");
    }
    if (status == CAMERA_BRIDGE_NO_CAMERA) {
        mp_raise_ValueError(MP_ERROR_TEXT("camera: no such camera id, see camera.list()"));
    }
    if (status == CAMERA_BRIDGE_NO_FRAME_RATE) {
        mp_raise_ValueError(MP_ERROR_TEXT("camera: unsupported frame_rate, see supported_frame_rate_ranges()"));
    }
    self->open = true;
    g_open_camera = self;

    int32_t info[5];
    if (!camera_bridge_open_info(info, &err)) {
        close_impl(self);
        raise_os_error_free(MP_EIO, err);
    }
    self->out_w = info[0];
    self->out_h = info[1];
    self->src_w = info[2];
    self->src_h = info[3];
    self->rotation = info[4];
    for (int i = 0; i < CAMERA_BRIDGE_SLOTS; i++) {
        size_t cap = 0;
        if (!camera_bridge_buffer(i, &self->slots[i], &cap, &err)) {
            close_impl(self);
            raise_os_error_free(MP_EIO, err);
        }
        if (cap < (size_t) self->src_w * self->src_h * (format == PIXFORMAT_RGB565 ? 4 : 1)) {
            close_impl(self);
            raise_os_error(MP_EIO, "camera: frame buffer too small");
        }
    }
    return MP_OBJ_FROM_PTR(self);
}

// Newest frame not returned before, upright, as image.Image.
mp_obj_t camera_snapshot(mp_obj_t self_in) {
    auto *self = (camera_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);

    // Allocated before a frame is held, so a MemoryError holds nothing.
    image_t img = {};
    img.w = self->out_w;
    img.h = self->out_h;
    img.pixfmt = self->pixfmt;
    image_alloc_tf_aligned(&img, image_size(&img));

    long budget = self->last_seq == 0 ? kFirstFrameTimeoutMs : kFrameTimeoutMs;
    long start = now_ms();
    camera_frame_t frame;
    for (;;) {
        bool has_frame = false;
        char *err = nullptr;
        if (!camera_bridge_acquire(self->last_seq, kPollMs, &has_frame, &frame, &err)) {
            // Lost: the camera counts as closed from here on.
            close_impl(self);
            raise_os_error_free(MP_EIO, err);
        }
        if (has_frame) {
            break;
        }
        // Raises if the user tapped Interrupt.
        mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
        if (now_ms() - start >= budget) {
            raise_os_error(MP_ETIMEDOUT, "camera: no frame from the camera");
        }
    }
    if (frame.width != self->src_w || frame.height != self->src_h || frame.rotation_degrees != self->rotation) {
        camera_bridge_release();
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("camera: got a %dx%d frame rotated %d, expected %dx%d rotated %d"),
            (int) frame.width, (int) frame.height, (int) frame.rotation_degrees,
            (int) self->src_w, (int) self->src_h, (int) self->rotation);
    }
    if (self->pixfmt == PIXFORMAT_RGB565) {
        rotate_rgba_to_rgb565(self->slots[frame.slot], self->src_w, self->src_h, self->rotation, (uint16_t *) img.data);
    } else {
        rotate_gray(self->slots[frame.slot], self->src_w, self->src_h, self->rotation, img.data);
    }
    camera_bridge_release();
    self->last_seq = frame.seq;
    return py_image_from_struct(&img);
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_snapshot_obj, camera_snapshot);

mp_obj_t camera_size(mp_obj_t self_in) {
    auto *self = (camera_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);
    mp_obj_t wh[2] = {MP_OBJ_NEW_SMALL_INT(self->out_w), MP_OBJ_NEW_SMALL_INT(self->out_h)};
    return mp_obj_new_tuple(2, wh);
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_size_obj, camera_size);

mp_obj_t camera_close(mp_obj_t self_in) {
    close_impl((camera_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_close_obj, camera_close);

mp_obj_t camera_enter(mp_obj_t self_in) {
    return self_in;
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_enter_obj, camera_enter);

mp_obj_t camera_exit(size_t n_args, const mp_obj_t *args) {
    (void) n_args;
    close_impl((camera_obj_t *) MP_OBJ_TO_PTR(args[0]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(camera_exit_obj, 4, 4, camera_exit);

// Raises the error a control or info call reported. msg is malloc'd.
[[noreturn]] void raise_kind(int kind, char *msg) {
    if (kind == CAMERA_BRIDGE_ERR_VALUE) {
        mp_obj_t text = mp_obj_new_str(msg, strlen(msg));
        free(msg);
        nlr_raise(mp_obj_new_exception_arg1(&mp_type_ValueError, text));
    }
    raise_os_error_free(kind == CAMERA_BRIDGE_ERR_UNSUPPORTED ? MP_ENODEV : MP_EIO, msg);
    for (;;) {}
}

// Runs one CameraControl operation and waits for CameraX to confirm it,
// in kPollMs steps so Interrupt is seen. Returns its value.
double run_control(mp_obj_t self_in, int op, float a, float b, int n, int flags) {
    auto *self = (camera_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);
    int kind = 0;
    char *err = nullptr;
    if (!camera_bridge_control_start(op, a, b, n, flags, &kind, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    if (kind == CAMERA_BRIDGE_ERR_LOST) {
        close_impl(self);
    }
    if (kind != 0) {
        raise_kind(kind, err);
    }
    long start = now_ms();
    for (;;) {
        bool done = false;
        double value = 0;
        if (!camera_bridge_control_wait(kPollMs, &done, &kind, &value, &err)) {
            raise_os_error_free(MP_EIO, err);
        }
        if (done) {
            if (kind != 0) {
                raise_kind(kind, err);
            }
            return value;
        }
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
            nlr_pop();
        } else {
            camera_bridge_control_abandon();
            nlr_jump(nlr.ret_val);
        }
        if (now_ms() - start >= kControlTimeoutMs) {
            camera_bridge_control_abandon();
            raise_os_error(MP_ETIMEDOUT, "camera: CameraX did not confirm the control");
        }
    }
}

mp_obj_t camera_enable_torch(mp_obj_t self_in, mp_obj_t on_in) {
    run_control(self_in, CAMERA_BRIDGE_OP_TORCH, mp_obj_is_true(on_in) ? 1.0f : 0.0f, 0, 0, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(camera_enable_torch_obj, camera_enable_torch);

mp_obj_t camera_set_torch_strength_level(mp_obj_t self_in, mp_obj_t level_in) {
    run_control(self_in, CAMERA_BRIDGE_OP_TORCH_STRENGTH, 0, 0, (int) mp_obj_get_int(level_in), 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(camera_set_torch_strength_level_obj, camera_set_torch_strength_level);

mp_obj_t camera_enable_low_light_boost(mp_obj_t self_in, mp_obj_t on_in) {
    run_control(self_in, CAMERA_BRIDGE_OP_LOW_LIGHT, mp_obj_is_true(on_in) ? 1.0f : 0.0f, 0, 0, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(camera_enable_low_light_boost_obj, camera_enable_low_light_boost);

mp_obj_t camera_set_zoom_ratio(mp_obj_t self_in, mp_obj_t ratio_in) {
    run_control(self_in, CAMERA_BRIDGE_OP_ZOOM_RATIO, (float) mp_obj_get_float(ratio_in), 0, 0, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(camera_set_zoom_ratio_obj, camera_set_zoom_ratio);

mp_obj_t camera_set_linear_zoom(mp_obj_t self_in, mp_obj_t zoom_in) {
    run_control(self_in, CAMERA_BRIDGE_OP_LINEAR_ZOOM, (float) mp_obj_get_float(zoom_in), 0, 0, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(camera_set_linear_zoom_obj, camera_set_linear_zoom);

mp_obj_t camera_set_exposure_compensation_index(mp_obj_t self_in, mp_obj_t index_in) {
    double v = run_control(self_in, CAMERA_BRIDGE_OP_EXPOSURE, 0, 0, (int) mp_obj_get_int(index_in), 0);
    return mp_obj_new_int((mp_int_t) v);
}
static MP_DEFINE_CONST_FUN_OBJ_2(camera_set_exposure_compensation_index_obj, camera_set_exposure_compensation_index);

// x, y in snapshot coordinates. Returns whether focus succeeded.
mp_obj_t camera_start_focus_and_metering(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_x, ARG_y, ARG_auto_cancel_ms, ARG_af, ARG_ae, ARG_awb };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_x, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_y, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_auto_cancel_ms, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 5000}},
        {MP_QSTR_af, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true}},
        {MP_QSTR_ae, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true}},
        {MP_QSTR_awb, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true}},
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    int flags = (parsed[ARG_af].u_bool ? CAMERA_BRIDGE_FOCUS_AF : 0) |
                (parsed[ARG_ae].u_bool ? CAMERA_BRIDGE_FOCUS_AE : 0) |
                (parsed[ARG_awb].u_bool ? CAMERA_BRIDGE_FOCUS_AWB : 0);
    if (flags == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("camera: at least one of af, ae, awb"));
    }
    double ok = run_control(pos_args[0], CAMERA_BRIDGE_OP_FOCUS, (float) mp_obj_get_float(parsed[ARG_x].u_obj),
                            (float) mp_obj_get_float(parsed[ARG_y].u_obj), (int) parsed[ARG_auto_cancel_ms].u_int, flags);
    return mp_obj_new_bool(ok != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(camera_start_focus_and_metering_obj, 3, camera_start_focus_and_metering);

mp_obj_t camera_cancel_focus_and_metering(mp_obj_t self_in) {
    run_control(self_in, CAMERA_BRIDGE_OP_CANCEL_FOCUS, 0, 0, 0, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_cancel_focus_and_metering_obj, camera_cancel_focus_and_metering);

// CameraInfo numbers for key; the caller frees *out_values.
size_t info_numbers(mp_obj_t self_in, int key, float x, float y, double **out_values) {
    auto *self = (camera_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);
    bool open = false;
    size_t n = 0;
    char *err = nullptr;
    if (!camera_bridge_info_numbers(key, x, y, &open, out_values, &n, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    if (!open) {
        close_impl(self);
        raise_os_error(MP_EIO, "camera: lost -- open a new camera.Camera()");
    }
    return n;
}

double info_number(mp_obj_t self_in, int key) {
    double *v = nullptr;
    size_t n = info_numbers(self_in, key, 0, 0, &v);
    double out = n > 0 ? v[0] : 0;
    free(v);
    return out;
}

mp_obj_t info_string(mp_obj_t self_in, int key) {
    auto *self = (camera_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);
    bool open = false;
    char *value = nullptr;
    char *err = nullptr;
    if (!camera_bridge_info_string(key, &open, &value, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    if (!open) {
        close_impl(self);
        raise_os_error(MP_EIO, "camera: lost -- open a new camera.Camera()");
    }
    mp_obj_t out = mp_obj_new_str(value, strlen(value));
    free(value);
    return out;
}

#define INFO_INT(name, key) \
    mp_obj_t camera_##name(mp_obj_t self_in) { return mp_obj_new_int((mp_int_t) info_number(self_in, key)); } \
    static MP_DEFINE_CONST_FUN_OBJ_1(camera_##name##_obj, camera_##name);
#define INFO_BOOL(name, key) \
    mp_obj_t camera_##name(mp_obj_t self_in) { return mp_obj_new_bool(info_number(self_in, key) != 0); } \
    static MP_DEFINE_CONST_FUN_OBJ_1(camera_##name##_obj, camera_##name);
#define INFO_STR(name, key) \
    mp_obj_t camera_##name(mp_obj_t self_in) { return info_string(self_in, key); } \
    static MP_DEFINE_CONST_FUN_OBJ_1(camera_##name##_obj, camera_##name);

INFO_INT(sensor_rotation_degrees, CAMERA_BRIDGE_INFO_SENSOR_ROTATION)
INFO_BOOL(has_flash_unit, CAMERA_BRIDGE_INFO_HAS_FLASH)
INFO_BOOL(torch_state, CAMERA_BRIDGE_INFO_TORCH_STATE)
INFO_BOOL(is_torch_strength_supported, CAMERA_BRIDGE_INFO_TORCH_STRENGTH_SUPPORTED)
INFO_INT(max_torch_strength_level, CAMERA_BRIDGE_INFO_MAX_TORCH_STRENGTH)
INFO_INT(torch_strength_level, CAMERA_BRIDGE_INFO_TORCH_STRENGTH)
INFO_BOOL(is_low_light_boost_supported, CAMERA_BRIDGE_INFO_LOW_LIGHT_SUPPORTED)
INFO_BOOL(is_logical_multi_camera_supported, CAMERA_BRIDGE_INFO_LOGICAL_MULTI_CAMERA)
INFO_STR(lens_facing, CAMERA_BRIDGE_INFO_LENS_FACING)
INFO_STR(implementation_type, CAMERA_BRIDGE_INFO_IMPLEMENTATION_TYPE)
INFO_STR(low_light_boost_state, CAMERA_BRIDGE_INFO_LOW_LIGHT_STATE)

mp_obj_t camera_intrinsic_zoom_ratio(mp_obj_t self_in) {
    return mp_obj_new_float((mp_float_t) info_number(self_in, CAMERA_BRIDGE_INFO_INTRINSIC_ZOOM));
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_intrinsic_zoom_ratio_obj, camera_intrinsic_zoom_ratio);

// {"ratio", "min", "max", "linear"}
mp_obj_t camera_zoom_state(mp_obj_t self_in) {
    double *v = nullptr;
    size_t n = info_numbers(self_in, CAMERA_BRIDGE_INFO_ZOOM_STATE, 0, 0, &v);
    mp_obj_t d = mp_obj_new_dict(4);
    qstr keys[4] = {MP_QSTR_ratio, MP_QSTR_min, MP_QSTR_max, MP_QSTR_linear};
    for (size_t i = 0; i < 4 && i < n; i++) {
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(keys[i]), mp_obj_new_float((mp_float_t) v[i]));
    }
    free(v);
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_zoom_state_obj, camera_zoom_state);

// {"index", "min", "max", "step", "supported"}
mp_obj_t camera_exposure_state(mp_obj_t self_in) {
    double *v = nullptr;
    size_t n = info_numbers(self_in, CAMERA_BRIDGE_INFO_EXPOSURE_STATE, 0, 0, &v);
    mp_obj_t d = mp_obj_new_dict(5);
    if (n >= 5) {
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_index), mp_obj_new_int((mp_int_t) v[0]));
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_min), mp_obj_new_int((mp_int_t) v[1]));
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_max), mp_obj_new_int((mp_int_t) v[2]));
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_step), mp_obj_new_float((mp_float_t) v[3]));
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_supported), mp_obj_new_bool(v[4] != 0));
    }
    free(v);
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_exposure_state_obj, camera_exposure_state);

mp_obj_t camera_is_focus_metering_supported(mp_obj_t self_in, mp_obj_t x_in, mp_obj_t y_in) {
    double *v = nullptr;
    size_t n = info_numbers(self_in, CAMERA_BRIDGE_INFO_FOCUS_SUPPORTED, (float) mp_obj_get_float(x_in),
                            (float) mp_obj_get_float(y_in), &v);
    bool ok = n > 0 && v[0] != 0;
    free(v);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_3(camera_is_focus_metering_supported_obj, camera_is_focus_metering_supported);

// [(min, max), ...]
mp_obj_t camera_supported_frame_rate_ranges(mp_obj_t self_in) {
    double *v = nullptr;
    size_t n = info_numbers(self_in, CAMERA_BRIDGE_INFO_FRAME_RATE_RANGES, 0, 0, &v);
    mp_obj_t list = mp_obj_new_list(0, nullptr);
    for (size_t i = 0; i + 1 < n; i += 2) {
        mp_obj_t pair[2] = {mp_obj_new_int((mp_int_t) v[i]), mp_obj_new_int((mp_int_t) v[i + 1])};
        mp_obj_list_append(list, mp_obj_new_tuple(2, pair));
    }
    free(v);
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_supported_frame_rate_ranges_obj, camera_supported_frame_rate_ranges);

const mp_rom_map_elem_t camera_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_snapshot), MP_ROM_PTR(&camera_snapshot_obj)},
    {MP_ROM_QSTR(MP_QSTR_size), MP_ROM_PTR(&camera_size_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&camera_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&camera_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&camera_enter_obj)},
    {MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&camera_exit_obj)},
    {MP_ROM_QSTR(MP_QSTR_enable_torch), MP_ROM_PTR(&camera_enable_torch_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_torch_strength_level), MP_ROM_PTR(&camera_set_torch_strength_level_obj)},
    {MP_ROM_QSTR(MP_QSTR_enable_low_light_boost), MP_ROM_PTR(&camera_enable_low_light_boost_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_zoom_ratio), MP_ROM_PTR(&camera_set_zoom_ratio_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_linear_zoom), MP_ROM_PTR(&camera_set_linear_zoom_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_exposure_compensation_index), MP_ROM_PTR(&camera_set_exposure_compensation_index_obj)},
    {MP_ROM_QSTR(MP_QSTR_start_focus_and_metering), MP_ROM_PTR(&camera_start_focus_and_metering_obj)},
    {MP_ROM_QSTR(MP_QSTR_cancel_focus_and_metering), MP_ROM_PTR(&camera_cancel_focus_and_metering_obj)},
    {MP_ROM_QSTR(MP_QSTR_sensor_rotation_degrees), MP_ROM_PTR(&camera_sensor_rotation_degrees_obj)},
    {MP_ROM_QSTR(MP_QSTR_lens_facing), MP_ROM_PTR(&camera_lens_facing_obj)},
    {MP_ROM_QSTR(MP_QSTR_intrinsic_zoom_ratio), MP_ROM_PTR(&camera_intrinsic_zoom_ratio_obj)},
    {MP_ROM_QSTR(MP_QSTR_implementation_type), MP_ROM_PTR(&camera_implementation_type_obj)},
    {MP_ROM_QSTR(MP_QSTR_has_flash_unit), MP_ROM_PTR(&camera_has_flash_unit_obj)},
    {MP_ROM_QSTR(MP_QSTR_torch_state), MP_ROM_PTR(&camera_torch_state_obj)},
    {MP_ROM_QSTR(MP_QSTR_is_torch_strength_supported), MP_ROM_PTR(&camera_is_torch_strength_supported_obj)},
    {MP_ROM_QSTR(MP_QSTR_max_torch_strength_level), MP_ROM_PTR(&camera_max_torch_strength_level_obj)},
    {MP_ROM_QSTR(MP_QSTR_torch_strength_level), MP_ROM_PTR(&camera_torch_strength_level_obj)},
    {MP_ROM_QSTR(MP_QSTR_zoom_state), MP_ROM_PTR(&camera_zoom_state_obj)},
    {MP_ROM_QSTR(MP_QSTR_exposure_state), MP_ROM_PTR(&camera_exposure_state_obj)},
    {MP_ROM_QSTR(MP_QSTR_is_focus_metering_supported), MP_ROM_PTR(&camera_is_focus_metering_supported_obj)},
    {MP_ROM_QSTR(MP_QSTR_supported_frame_rate_ranges), MP_ROM_PTR(&camera_supported_frame_rate_ranges_obj)},
    {MP_ROM_QSTR(MP_QSTR_is_low_light_boost_supported), MP_ROM_PTR(&camera_is_low_light_boost_supported_obj)},
    {MP_ROM_QSTR(MP_QSTR_low_light_boost_state), MP_ROM_PTR(&camera_low_light_boost_state_obj)},
    {MP_ROM_QSTR(MP_QSTR_is_logical_multi_camera_supported), MP_ROM_PTR(&camera_is_logical_multi_camera_supported_obj)},
};
MP_DEFINE_CONST_DICT(camera_locals_dict, camera_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    camera_type,
    MP_QSTR_Camera,
    MP_TYPE_FLAG_NONE,
    make_new, camera_make_new,
    locals_dict, &camera_locals_dict
    );

// Keep in sync with the tables in this file, see DEVELOPER.md.
const char camera_help_text[] =
    "module: camera (CameraX; not OpenMV csi)\n"
    "functions:\n"
    "  camera.list(): [{\"id\": str, \"facing\": \"back\"|\"front\"|\"external\"|\"unknown\", \"sizes\": [(w, h), ...]}, ...]; sizes in sensor orientation (landscape), largest first\n"
    "class: camera.Camera(id=None, size=(320, 240), format=camera.GRAYSCALE, frame_rate=None)\n"
    "  id: from camera.list(); None: the first back camera\n"
    "  size: the listed size with the nearest pixel count is used (orientation of the request does not matter); see size()\n"
    "  format: camera.GRAYSCALE or camera.RGB565 (CameraX converts to RGB)\n"
    "  frame_rate: None (automatic; drops in dim light) or (min, max) from supported_frame_rate_ranges(), e.g. (30, 30)\n"
    "  frames are upright as the phone is held when the camera opens; this does not change while it is open, and the screen locks to it\n"
    "  front camera images are not mirrored\n"
    "  one camera open at a time: a new Camera() closes the open one; large sizes need a larger heap (app Settings)\n"
    "  stays open until close(), a new Camera(), garbage collection or reset, also after the script ends or Interrupt\n"
    "  with camera.Camera() as cam: ... closes it when the block ends\n"
    "methods:\n"
    "  snapshot(): the newest frame not returned before, as image.Image (64-byte aligned); waits for it\n"
    "  size(): (w, h) of the images snapshot() returns, e.g. (240, 320) for 320x240 held upright\n"
    "  close(): release the camera\n"
    "CameraControl methods (CameraX names; each waits until CameraX confirms, at most 5 s):\n"
    "  enable_torch(on)\n"
    "  set_torch_strength_level(level): 1..max_torch_strength_level()\n"
    "  enable_low_light_boost(on)\n"
    "  set_zoom_ratio(ratio): zoom_state()[\"min\"]..[\"max\"]\n"
    "  set_linear_zoom(zoom): 0.0..1.0\n"
    "  set_exposure_compensation_index(index): exposure_state()[\"min\"]..[\"max\"], in steps of [\"step\"] EV; returns the index set\n"
    "  start_focus_and_metering(x, y, *, auto_cancel_ms=5000, af=True, ae=True, awb=True): x, y in snapshot coordinates; auto_cancel_ms=0 keeps it; returns True if focus succeeded\n"
    "  cancel_focus_and_metering()\n"
    "CameraInfo methods:\n"
    "  sensor_rotation_degrees(), lens_facing(), intrinsic_zoom_ratio(), implementation_type()\n"
    "  has_flash_unit(), torch_state(): bool\n"
    "  is_torch_strength_supported(), max_torch_strength_level(), torch_strength_level()\n"
    "  zoom_state(): {\"ratio\", \"min\", \"max\", \"linear\"}\n"
    "  exposure_state(): {\"index\", \"min\", \"max\", \"step\", \"supported\"}\n"
    "  is_focus_metering_supported(x, y), supported_frame_rate_ranges(): [(min, max), ...]\n"
    "  is_low_light_boost_supported(), low_light_boost_state(): \"off\"|\"inactive\"|\"active\"\n"
    "  is_logical_multi_camera_supported()\n"
    "  not provided (still photos, video, HDR or internal): must_play_shutter_sound, is_zsl_supported, is_private_reprocessing_supported, query_supported_dynamic_ranges, camera_state, physical_camera_infos, is_session_config_supported, camera_selector, camera_identifier\n"
    "constants: camera.GRAYSCALE, camera.RGB565\n"
    "errors:\n"
    "  OSError(EACCES): no camera permission (a prompt is shown; try again after allowing)\n"
    "  OSError(EIO): camera lost (screen off, app not in foreground, other app) or failed; it is then closed, open a new Camera()\n"
    "  OSError(ETIMEDOUT): no frame for 2 s (5 s for the first), or a control not confirmed in 5 s\n"
    "  OSError(ENODEV): control not supported by this camera (no flash unit, ...)\n"
    "  OSError(EINVAL): method called after the camera was closed (close(), lost, or taken over by a new Camera())\n"
    "  ValueError: no such camera id, bad size, format or frame_rate, control value out of range\n"
    "see_also: display.help(), image\n"
;

mp_obj_t camera_help() {
    return mp_obj_new_str(camera_help_text, strlen(camera_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_help_obj, camera_help);

const mp_rom_map_elem_t camera_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_camera)},
    {MP_ROM_QSTR(MP_QSTR_Camera), MP_ROM_PTR(&camera_type)},
    {MP_ROM_QSTR(MP_QSTR_list), MP_ROM_PTR(&camera_list_obj)},
    {MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&camera_help_obj)},
    {MP_ROM_QSTR(MP_QSTR_GRAYSCALE), MP_ROM_INT(PIXFORMAT_GRAYSCALE)},
    {MP_ROM_QSTR(MP_QSTR_RGB565), MP_ROM_INT(PIXFORMAT_RGB565)},
};
MP_DEFINE_CONST_DICT(camera_module_globals, camera_module_globals_table);

}  // namespace

// C linkage: genhdr/moduledefs.h declares it.
extern "C" const mp_obj_module_t camera_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &camera_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_camera, camera_module);

extern "C" void camera_bridge_init(void *jni_env) {
    camera_bridge_init_impl(jni_env);
}

extern "C" void camera_close_all(void) {
    if (g_open_camera) {
        close_impl(g_open_camera);
    }
}

extern "C" void camera_interrupt_active_wait(void) {
    camera_bridge_interrupt();
}

extern "C" void camera_get_current_size(int32_t *width, int32_t *height, bool *color) {
    if (g_open_camera) {
        *width = g_open_camera->out_w;
        *height = g_open_camera->out_h;
        *color = g_open_camera->pixfmt == PIXFORMAT_RGB565;
    } else {
        *width = kDefaultWidth;
        *height = kDefaultHeight;
        *color = false;
    }
}
