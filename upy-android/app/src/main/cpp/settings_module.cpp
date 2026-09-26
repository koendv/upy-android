// upy-android native settings module (android.settings). OUR OWN code,
// NOT vendored OpenMV source. Read-only from MicroPython: values are
// pushed from Kotlin via engine_jni.cpp's nativeSetSettings(), never
// written here. A function returning a fresh dict, not a const module
// dict, so every call reflects the latest push rather than a
// build-time-frozen snapshot. Passwords (ssh/http) are never exposed --
// see SettingsManager.kt.

extern "C" {
#include "py/obj.h"
#include "py/runtime.h"
}

#include "settings_state.h"

namespace {

mp_obj_t android_settings() {
    SettingsSnapshot s = settings_snapshot_get();
    mp_obj_t dict = mp_obj_new_dict(6);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_heap_size_mb), mp_obj_new_int(s.heap_size_mb));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_ssh_enabled), s.ssh_enabled ? mp_const_true : mp_const_false);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_http_server_enabled), s.http_server_enabled ? mp_const_true : mp_const_false);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_http_private_files_enabled), s.http_private_files_enabled ? mp_const_true : mp_const_false);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_litert_playstore_enabled), s.litert_playstore_enabled ? mp_const_true : mp_const_false);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_adb_exec_enabled), s.adb_exec_enabled ? mp_const_true : mp_const_false);
    return dict;
}

} // namespace

extern MP_DEFINE_CONST_FUN_OBJ_0(android_settings_obj, android_settings);
