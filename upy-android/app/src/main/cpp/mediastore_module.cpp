// upy-android native mediastore module (android.mediastore). OUR OWN
// code, NOT vendored OpenMV source. Write-only -- see this file's own
// header comment in mediastore_module.h and the plan's own Part 7
// design (SESSION_STATE.yaml).

extern "C" {
#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"
}

#include <cstdlib>
#include <cstring>

#include "mediastore_jni_bridge.h"
#include "mediastore_module.h"

namespace {

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// android.mediastore.save_image(data, name, mime_type='image/jpeg') -> str
// data: anything with the buffer protocol (bytes/bytearray) -- a
// script's own already-encoded image (e.g. img.compress().bytearray()).
// This module doesn't know or care about pixel formats, same "just
// bytes" contract as ordinary file I/O. Returns the new item's real
// content:// URI as a string.
mp_obj_t mediastore_save_image(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_data, ARG_name, ARG_mime_type };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_data, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_name, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_mime_type, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_data].u_obj, &bufinfo, MP_BUFFER_READ);
    const char *name = mp_obj_str_get_str(args[ARG_name].u_obj);
    const char *mime_type = args[ARG_mime_type].u_obj != MP_OBJ_NULL
        ? mp_obj_str_get_str(args[ARG_mime_type].u_obj)
        : "image/jpeg";

    char *out_uri = nullptr;
    char *out_err = nullptr;
    bool ok = mediastore_bridge_save_image(
        (const uint8_t *) bufinfo.buf, bufinfo.len, name, mime_type, &out_uri, &out_err);

    if (!ok) {
        char buf[192];
        snprintf(buf, sizeof(buf), "android.mediastore: %s", out_err ? out_err : "unknown error");
        free(out_err);
        raise_os_error(MP_EIO, buf);
    }

    mp_obj_t result = mp_obj_new_str(out_uri, strlen(out_uri));
    free(out_uri);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mediastore_save_image_obj, 2, mediastore_save_image);

const mp_rom_map_elem_t mediastore_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_mediastore)},
    {MP_ROM_QSTR(MP_QSTR_save_image), MP_ROM_PTR(&mediastore_save_image_obj)},
};
MP_DEFINE_CONST_DICT(mediastore_globals, mediastore_globals_table);

}  // namespace

// extern "C" + extern prefix required, same MP_DEFINE_CONST_OBJ_TYPE/
// module-object internal-linkage reasoning as every other nested
// android.* submodule (see android_module.cpp's own tail comments).
extern "C" const mp_obj_module_t mediastore_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &mediastore_globals,
};

extern "C" void mediastore_bridge_init(void *jni_env, void *application_context) {
    mediastore_bridge_init_impl(jni_env, application_context);
}
