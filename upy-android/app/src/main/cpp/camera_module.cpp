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

// Rotates a W x H grayscale frame clockwise by rot (0/90/180/270) into
// dst. Each output row is a straight line through the source, so only
// a start index and a step per pixel are needed.
void rotate_gray(const uint8_t *src, int32_t W, int32_t H, int32_t rot, uint8_t *dst) {
    if (rot == 0) {
        memcpy(dst, src, (size_t) W * H);
        return;
    }
    int32_t out_w = (rot == 180) ? W : H;
    int32_t out_h = (rot == 180) ? H : W;
    ptrdiff_t step = (rot == 90) ? -W : (rot == 180) ? -1 : W;
    for (int32_t oy = 0; oy < out_h; oy++) {
        ptrdiff_t i = (rot == 90) ? (ptrdiff_t) (H - 1) * W + oy
                    : (rot == 180) ? (ptrdiff_t) (H - 1 - oy) * W + (W - 1)
                    : (ptrdiff_t) (W - 1 - oy);
        uint8_t *row = dst + (size_t) oy * out_w;
        for (int32_t ox = 0; ox < out_w; ox++, i += step) {
            row[ox] = src[i];
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
    enum { ARG_id, ARG_size, ARG_format };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_id, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        {MP_QSTR_size, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        {MP_QSTR_format, MP_ARG_INT, {.u_int = PIXFORMAT_GRAYSCALE}},
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
    mp_int_t format = parsed[ARG_format].u_int;
    if (format == PIXFORMAT_RGB565) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("camera: RGB565 not supported yet"));
    }
    if (format != PIXFORMAT_GRAYSCALE) {
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
    if (!camera_bridge_open(id, want_w, want_h, &status, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    if (status == CAMERA_BRIDGE_NO_PERMISSION) {
        raise_os_error(MP_EACCES, "camera: no camera permission -- allow it in the prompt or in Android Settings, then try again");
    }
    if (status == CAMERA_BRIDGE_NO_CAMERA) {
        mp_raise_ValueError(MP_ERROR_TEXT("camera: no such camera id, see camera.list()"));
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
        if (cap < (size_t) self->src_w * self->src_h) {
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
    rotate_gray(self->slots[frame.slot], self->src_w, self->src_h, self->rotation, img.data);
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

const mp_rom_map_elem_t camera_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_snapshot), MP_ROM_PTR(&camera_snapshot_obj)},
    {MP_ROM_QSTR(MP_QSTR_size), MP_ROM_PTR(&camera_size_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&camera_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&camera_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&camera_enter_obj)},
    {MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&camera_exit_obj)},
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
    "class: camera.Camera(id=None, size=(320, 240), format=camera.GRAYSCALE)\n"
    "  id: from camera.list(); None: the first back camera\n"
    "  size: the listed size with the nearest pixel count is used (orientation of the request does not matter); see size()\n"
    "  format: camera.GRAYSCALE (camera.RGB565 not supported yet)\n"
    "  frames are upright as the phone is held when the camera opens; this does not change while it is open, and the screen locks to it\n"
    "  front camera images are not mirrored\n"
    "  one camera open at a time: a new Camera() closes the open one; large sizes need a larger heap (app Settings)\n"
    "  stays open until close(), a new Camera(), garbage collection or reset, also after the script ends or Interrupt\n"
    "  with camera.Camera() as cam: ... closes it when the block ends\n"
    "methods:\n"
    "  snapshot(): the newest frame not returned before, as image.Image (64-byte aligned); waits for it\n"
    "  size(): (w, h) of the images snapshot() returns, e.g. (240, 320) for 320x240 held upright\n"
    "  close(): release the camera\n"
    "constants: camera.GRAYSCALE, camera.RGB565\n"
    "errors:\n"
    "  OSError(EACCES): no camera permission (a prompt is shown; try again after allowing)\n"
    "  OSError(EIO): camera lost (screen off, app not in foreground, other app) or failed; it is then closed, open a new Camera()\n"
    "  OSError(ETIMEDOUT): no frame for 2 s (5 s for the first)\n"
    "  OSError(EINVAL): method called after the camera was closed (close(), lost, or taken over by a new Camera())\n"
    "  ValueError: no such camera id, bad size or format\n"
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
