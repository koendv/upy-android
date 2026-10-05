// upy-android native display module. A bridge between
// py_image.c's Image type and a real Android Surface. Not vendored
// OpenMV source.
// see session-state: display_module.cpp#module_design

#include "display_module.h"

#include <android/native_window.h>
#include <android/log.h>
#include <pthread.h>
#include <string.h>
#include <initializer_list>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "imlib.h"
#include "py_image.h"
}

#define LOG_TAG "upy-display"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

// see session-state: display_module.cpp#g_window_mutex
pthread_mutex_t g_window_mutex = PTHREAD_MUTEX_INITIALIZER;
ANativeWindow *g_window = nullptr;

typedef struct _display_obj_t {
    mp_obj_base_t base;
    bool vflip;
    bool hmirror;
} display_obj_t;

// Largest of {1,2,4,8} such that BOTH src_w*factor<=buf_w AND
// src_h*factor<=buf_h.
// see session-state: display_module.cpp#module_design
int upscale_factor(int32_t src_w, int32_t src_h, int32_t buf_w, int32_t buf_h) {
    int factor = 1;
    for (int candidate : {2, 4, 8}) {
        if (src_w * candidate <= buf_w && src_h * candidate <= buf_h) {
            factor = candidate;
        }
    }
    return factor;
}

mp_obj_t display_write(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_x, ARG_y, ARG_hint };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_x, MP_ARG_INT | MP_ARG_KW_ONLY, {.u_int = 0}},
        {MP_QSTR_y, MP_ARG_INT | MP_ARG_KW_ONLY, {.u_int = 0}},
        {MP_QSTR_hint, MP_ARG_INT | MP_ARG_KW_ONLY, {.u_int = 0}},
    };
    (void) ARG_x;
    (void) ARG_y;

    auto *self = static_cast<display_obj_t *>(MP_OBJ_TO_PTR(pos_args[0]));
    image_t *src = (image_t *) py_image_cobj(pos_args[1]);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 2, pos_args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    // see session-state: display_module.cpp#module_design
    int32_t hint = args[ARG_hint].u_int;
    bool transpose = (hint & IMAGE_HINT_TRANSPOSE) != 0;
    bool hint_hmirror = (hint & IMAGE_HINT_HMIRROR) != 0;
    bool hint_vflip = (hint & IMAGE_HINT_VFLIP) != 0;

    pthread_mutex_lock(&g_window_mutex);
    if (!g_window) {
        pthread_mutex_unlock(&g_window_mutex);
        return mp_const_none;
    }

    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(g_window, &buffer, nullptr) != 0) {
        // Stale/torn-down window that hasn't been detached via
        // setDisplaySurface(null) yet. Treat the same as "no window",
        // not an error.
        pthread_mutex_unlock(&g_window_mutex);
        return mp_const_none;
    }

    // see session-state: display_module.cpp#module_design
    int32_t logical_w = transpose ? src->h : src->w;
    int32_t logical_h = transpose ? src->w : src->h;

    int factor = upscale_factor(logical_w, logical_h, buffer.width, buffer.height);
    int32_t scaled_w = logical_w * factor;
    int32_t scaled_h = logical_h * factor;
    int32_t off_x = (buffer.width - scaled_w) / 2;
    int32_t off_y = (buffer.height - scaled_h) / 2;

    auto *pixels = (uint32_t *) buffer.bits;
    // Opaque black. Fills the letterbox/pillarbox margin in one pass.
    memset(pixels, 0, (size_t) buffer.stride * buffer.height * 4);

    // see session-state: display_module.cpp#module_design
    bool flip_x = transpose ? hint_vflip : hint_hmirror;
    bool flip_y = transpose ? hint_hmirror : hint_vflip;

    for (int32_t ly = 0; ly < logical_h; ly++) {
        int32_t hy = self->vflip ? (logical_h - 1 - ly) : ly;
        for (int32_t lx = 0; lx < logical_w; lx++) {
            int32_t hx = self->hmirror ? (logical_w - 1 - lx) : lx;

            int32_t read_x, read_y;
            if (!transpose) {
                read_x = flip_x ? (src->w - 1 - hx) : hx;
                read_y = flip_y ? (src->h - 1 - hy) : hy;
            } else {
                read_x = flip_x ? (src->w - 1 - hy) : hy;
                read_y = flip_y ? (src->h - 1 - hx) : hx;
            }

            uint8_t r, g, b;
            if (src->pixfmt == PIXFORMAT_RGB565) {
                uint16_t rgb565 = IMAGE_GET_RGB565_PIXEL(src, read_x, read_y);
                r = COLOR_RGB565_TO_R8(rgb565);
                g = COLOR_RGB565_TO_G8(rgb565);
                b = COLOR_RGB565_TO_B8(rgb565);
            } else {
                r = g = b = IMAGE_GET_GRAYSCALE_PIXEL(src, read_x, read_y);
            }
            uint32_t rgba = 0xFF000000u | ((uint32_t) b << 16) | ((uint32_t) g << 8) | r;

            int32_t dst_y0 = off_y + ly * factor;
            int32_t dst_x0 = off_x + lx * factor;
            for (int32_t dy = 0; dy < factor; dy++) {
                uint32_t *row = pixels + (size_t) (dst_y0 + dy) * buffer.stride + dst_x0;
                for (int32_t dx = 0; dx < factor; dx++) {
                    row[dx] = rgba;
                }
            }
        }
    }

    ANativeWindow_unlockAndPost(g_window);
    pthread_mutex_unlock(&g_window_mutex);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(display_write_obj, 2, display_write);

mp_obj_t display_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *pos_args) {
    enum { ARG_vflip, ARG_hmirror };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_vflip, MP_ARG_BOOL | MP_ARG_KW_ONLY, {.u_bool = false}},
        {MP_QSTR_hmirror, MP_ARG_BOOL | MP_ARG_KW_ONLY, {.u_bool = false}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, pos_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_obj_t *self = m_new_obj(display_obj_t);
    self->base.type = type;
    self->vflip = args[ARG_vflip].u_bool;
    self->hmirror = args[ARG_hmirror].u_bool;
    return MP_OBJ_FROM_PTR(self);
}

// Keep display_help_text (display.help(), below) in sync with this
// table -- see DEVELOPER.md's "adb server" section.
const mp_rom_map_elem_t display_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&display_write_obj)},
};
MP_DEFINE_CONST_DICT(display_locals_dict, display_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    spi_display_type,
    MP_QSTR_SPIDisplay,
    MP_TYPE_FLAG_NONE,
    make_new, display_make_new,
    locals_dict, &display_locals_dict
    );

// Runtime-queryable usage reference for an AI (or human) driving this
// module blind over adb, with no repo access -- see
// AdbExecProvider.kt/adb_help.yaml (help -> help('modules') -> import
// display; print(display.help())).
// see session-state: display_module.cpp#display_help
const char display_help_text[] =
    "module: display\n"
    "class: display.SPIDisplay(vflip=False, hmirror=False)\n"
    "  draws to the app's on-screen camera-preview Surface; the name is carried over from OpenMV's API, there is no real SPI bus involved\n"
    "methods:\n"
    "  write(image, x=0, y=0, hint=0): draw an image_t (e.g. from camera.Camera().snapshot()) to the screen\n"
    "    image is upscaled by the largest integer factor (2x/4x/8x) that still fits the surface, letterboxed/pillarboxed in opaque black\n"
    "    hint: OpenMV's IMAGE_HINT_HMIRROR / IMAGE_HINT_VFLIP / IMAGE_HINT_TRANSPOSE bits, combined with the vflip/hmirror set at construction\n"
    "    no-op (returns None) if no Surface is currently attached, i.e. the app isn't showing the camera screen\n"
    "see_also: camera.help(), android.help()\n"
;
mp_obj_t display_help() {
    return mp_obj_new_str(display_help_text, strlen(display_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(display_help_obj, display_help);

const mp_rom_map_elem_t display_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_display)},
    {MP_ROM_QSTR(MP_QSTR_SPIDisplay), MP_ROM_PTR(&spi_display_type)},
    {MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&display_help_obj)},
};
MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);

} // namespace

// extern "C", not inside the anonymous namespace above. genhdr/
// moduledefs.h declares this with C linkage, same reasoning as
// camera_module in camera_module.cpp.
extern "C" const mp_obj_module_t display_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &display_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_display, display_module);

extern "C" void display_set_window(ANativeWindow *new_window) {
    pthread_mutex_lock(&g_window_mutex);
    if (g_window) {
        ANativeWindow_release(g_window);
    }
    g_window = new_window;
    if (g_window) {
        // Fixed format, current (real) Surface size. (0,0) keeps
        // whatever the SurfaceView's actual layout size already is.
        // see session-state: display_module.cpp#module_design
        ANativeWindow_setBuffersGeometry(g_window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    }
    pthread_mutex_unlock(&g_window_mutex);
}
