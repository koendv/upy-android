// upy-android native display module -- OUR OWN bridge between
// py_image.c's Image type and a real Android Surface, via
// ANativeWindow_lock()/write pixels/ANativeWindow_unlockAndPost(). NOT
// vendored OpenMV source -- see display_module.h and
// SESSION_STATE.yaml's camera/display scoping discussion.
//
// Exposes display.SPIDisplay -- OpenMV's real class name, kept verbatim
// per the API-naming decision (SESSION_STATE.yaml): the name is just a
// Python-level symbol a script imports/constructs, not a literal
// physical-SPI-bus claim, same reasoning as csi.CSI() not implying a
// real camera serial interface.
//
// Upscale/pixel-format decisions all per SESSION_STATE.yaml, implemented
// here: integer power-of-2 duplication (x1/x2/x4/x8, largest fitting
// BOTH source dimensions into the buffer), centered in a buffer sized to
// the Surface's ACTUAL dimensions (never a small buffer left to the
// compositor to smooth-scale -- see the upscale-approach decision for
// why that was rejected). Surface lifecycle: silent no-op when no window
// is attached, not an error and not an auto-interrupt (see the surface-
// lifecycle decision) -- write() just returns.

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

// Guards g_window itself (the pointer swap in display_set_window()) AND
// every lock/write/unlockAndPost sequence in write() below -- the real
// race the "silent no-op, not stale-handle UB" decision depends on:
// surfaceDestroyed() -> setDisplaySurface(null) can fire on a Binder
// thread while a running script's write() is mid-blit on the worker
// thread. Without this, display_set_window() could
// ANativeWindow_release() the window a concurrent write() is still
// using. The only lock this module needs (there is exactly one producer
// -- whichever thread called setDisplaySurface -- and one consumer --
// the worker thread inside write()).
pthread_mutex_t g_window_mutex = PTHREAD_MUTEX_INITIALIZER;
ANativeWindow *g_window = nullptr;

typedef struct _display_obj_t {
    mp_obj_base_t base;
    bool vflip;
    bool hmirror;
} display_obj_t;

// Largest of {1,2,4,8} such that BOTH src_w*factor<=buf_w AND
// src_h*factor<=buf_h -- not width alone (see SESSION_STATE.yaml: a
// width-only selection could overflow available height for a portrait-
// shaped source frame).
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
    (void) ARG_hint;

    auto *self = static_cast<display_obj_t *>(MP_OBJ_TO_PTR(pos_args[0]));
    image_t *src = (image_t *) py_image_cobj(pos_args[1]);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 2, pos_args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    // x=/y=/hint= accepted (real scripts, e.g. lcd_shield.py, pass hint=
    // image.CENTER | image.SCALE_ASPECT_KEEP) but not yet interpreted --
    // this module's own upscale design (SESSION_STATE.yaml) already
    // always centers and always keeps aspect via uniform power-of-2
    // scaling, which is what CENTER | SCALE_ASPECT_KEEP asks for. Real
    // per-hint-flag behavior (SCALE_ASPECT_IGNORE/EXPAND, explicit x/y
    // placement) is a later increment if a real script needs it.

    pthread_mutex_lock(&g_window_mutex);
    if (!g_window) {
        pthread_mutex_unlock(&g_window_mutex);
        return mp_const_none;
    }

    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(g_window, &buffer, nullptr) != 0) {
        // Stale/torn-down window that hasn't been detached via
        // setDisplaySurface(null) yet -- treat the same as "no window",
        // not an error, consistent with the no-op decision.
        pthread_mutex_unlock(&g_window_mutex);
        return mp_const_none;
    }

    int factor = upscale_factor(src->w, src->h, buffer.width, buffer.height);
    int32_t scaled_w = src->w * factor;
    int32_t scaled_h = src->h * factor;
    int32_t off_x = (buffer.width - scaled_w) / 2;
    int32_t off_y = (buffer.height - scaled_h) / 2;

    auto *pixels = (uint32_t *) buffer.bits;
    // Opaque black -- fills the letterbox/pillarbox margin in one pass
    // (see SESSION_STATE.yaml: one native write pass does both the
    // duplication and the centering, no separate scale-then-blit step).
    memset(pixels, 0, (size_t) buffer.stride * buffer.height * 4);

    for (int32_t sy = 0; sy < src->h; sy++) {
        int32_t read_y = self->vflip ? (src->h - 1 - sy) : sy;
        for (int32_t sx = 0; sx < src->w; sx++) {
            int32_t read_x = self->hmirror ? (src->w - 1 - sx) : sx;

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

            int32_t dst_y0 = off_y + sy * factor;
            int32_t dst_x0 = off_x + sx * factor;
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

const mp_rom_map_elem_t display_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_display)},
    {MP_ROM_QSTR(MP_QSTR_SPIDisplay), MP_ROM_PTR(&spi_display_type)},
};
MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);

} // namespace

// extern "C" (not inside the anonymous namespace above) -- same linkage
// reasoning as csi_module in camera_module.cpp: genhdr/moduledefs.h
// declares this with C linkage.
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
        // Fixed format, current (real) Surface size (0,0 keeps whatever
        // the SurfaceView's actual layout size already is) -- write()'s
        // own upscale math depends on the buffer being the Surface's
        // real display size, never a small buffer left for the
        // compositor to smooth-scale (see SESSION_STATE.yaml).
        ANativeWindow_setBuffersGeometry(g_window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    }
    pthread_mutex_unlock(&g_window_mutex);
}
