// upy-android native fileprovider module (android.fileprovider). OUR
// OWN code, NOT vendored OpenMV source. See the plan's own Part 7
// design (SESSION_STATE.yaml) for the cross-process routing rationale --
// this module never starts an Activity itself, it only asks the main
// process to.

extern "C" {
#include "py/obj.h"
#include "py/runtime.h"
}

#include <cstdlib>
#include <cstring>

#include "fileprovider_jni_bridge.h"
#include "fileprovider_module.h"

namespace {

// android.fileprovider.share(path, mime_type='application/octet-stream')
// path: a VFS path (same root Explorer/Import/Export already expose,
// e.g. "/snapshot.jpg" or "snapshot.jpg" -- resolved against
// Context.filesDir on the main-process side, see FileProviderShim.kt).
// Fire-and-forget: returns None unconditionally once the request has
// been handed off. A missing/backgrounded main process silently drops
// the request (see fileprovider_jni_bridge.h's own contract comment) --
// this is not observable from the script, by design, matching the
// oneway AIDL interface's own semantics.
mp_obj_t fileprovider_share(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_path, ARG_mime_type };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_path, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_mime_type, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *path = mp_obj_str_get_str(args[ARG_path].u_obj);
    const char *mime_type = args[ARG_mime_type].u_obj != MP_OBJ_NULL
        ? mp_obj_str_get_str(args[ARG_mime_type].u_obj)
        : "application/octet-stream";

    char *out_err = nullptr;
    bool ok = fileprovider_bridge_share(path, mime_type, &out_err);
    if (!ok) {
        // A genuine local JNI failure, not "nothing was listening" --
        // see fileprovider_jni_bridge.h's own contract comment. Rare
        // enough (a resolution failure at bridge init would already
        // have surfaced) that a plain RuntimeException is enough, no
        // dedicated OSError mapping needed.
        char buf[192];
        snprintf(buf, sizeof(buf), "android.fileprovider: %s", out_err ? out_err : "unknown error");
        free(out_err);
        mp_raise_msg(&mp_type_RuntimeError, buf);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(fileprovider_share_obj, 1, fileprovider_share);

const mp_rom_map_elem_t fileprovider_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_fileprovider)},
    {MP_ROM_QSTR(MP_QSTR_share), MP_ROM_PTR(&fileprovider_share_obj)},
};
MP_DEFINE_CONST_DICT(fileprovider_globals, fileprovider_globals_table);

}  // namespace

// extern "C" + extern prefix required, same MP_DEFINE_CONST_OBJ_TYPE/
// module-object internal-linkage reasoning as every other nested
// android.* submodule (see android_module.cpp's own tail comments).
extern "C" const mp_obj_module_t fileprovider_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &fileprovider_globals,
};

extern "C" void fileprovider_bridge_init(void *jni_env) {
    fileprovider_bridge_init_impl(jni_env);
}
