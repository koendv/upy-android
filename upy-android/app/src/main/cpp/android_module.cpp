// upy-android native android module.
// micropython android port only.
// see session-state: android_module.cpp#module_design

#include <cstring>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
}

extern "C" const mp_obj_module_t imu_module;

extern const mp_obj_fun_builtin_fixed_t android_proximity_distance_cm_obj;
extern const mp_obj_fun_builtin_fixed_t android_settings_obj;

extern "C" const mp_obj_module_t mediastore_module;
extern "C" const mp_obj_module_t fileprovider_module;
extern "C" const mp_obj_module_t location_module;

namespace {

const mp_rom_map_elem_t android_proximity_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_proximity)},
    {MP_ROM_QSTR(MP_QSTR_distance_cm), MP_ROM_PTR(&android_proximity_distance_cm_obj)},
};
MP_DEFINE_CONST_DICT(android_proximity_globals, android_proximity_globals_table);

// Plain mp_obj_module_t, same as imu_module.
// see session-state: android_module.cpp#module_design
const mp_obj_module_t android_proximity_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &android_proximity_globals,
};

// Runtime-queryable usage reference for an AI (or human) driving this
// module blind over adb, with no repo access -- see
// AdbExecProvider.kt/adb_help.yaml (help -> help('modules') -> import
// android; print(android.help())).
// see session-state: android_module.cpp#android_help
const char android_help_text[] =
    "module: android (android-specific, no OpenMV equivalent)\n"
    "submodules:\n"
    "  android.proximity: distance_cm() -- proximity sensor reading in cm; OSError(ENODEV) if no sensor\n"
    "  android.imu: acceleration_mg()/angular_rate_mdps()/roll()/pitch()/sleep(enable) -- accelerometer/gyroscope\n"
    "  android.mediastore: save_image(data, name, mime_type=\"image/jpeg\") -- save bytes to the device's shared Photos/gallery; returns the new content:// URI\n"
    "  android.fileprovider: share(path, mime_type=\"application/octet-stream\") -- open Android's share sheet for a file; fire-and-forget, no return value, silently does nothing if no app window is focused\n"
    "  android.location: GPS/network location; see android.location.help()\n"
    "functions:\n"
    "  android.settings() -- dict snapshot of app settings (heap_size_mb, ssh_enabled, adb_exec_enabled); read-only here, set from the app UI\n"
    "see_also: camera.help(), display.help()\n"
;
mp_obj_t android_help() {
    return mp_obj_new_str(android_help_text, strlen(android_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(android_help_obj, android_help);

// Keep android_help_text (android.help(), above) in sync with this
// table -- see DEVELOPER.md's "adb server" section.
const mp_rom_map_elem_t android_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_android)},
    {MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&android_help_obj)},
    {MP_ROM_QSTR(MP_QSTR_imu), MP_ROM_PTR(&imu_module)},
    {MP_ROM_QSTR(MP_QSTR_proximity), MP_ROM_PTR(&android_proximity_module)},
    {MP_ROM_QSTR(MP_QSTR_settings), MP_ROM_PTR(&android_settings_obj)},
    {MP_ROM_QSTR(MP_QSTR_mediastore), MP_ROM_PTR(&mediastore_module)},
    {MP_ROM_QSTR(MP_QSTR_fileprovider), MP_ROM_PTR(&fileprovider_module)},
    {MP_ROM_QSTR(MP_QSTR_location), MP_ROM_PTR(&location_module)},
};
MP_DEFINE_CONST_DICT(android_module_globals, android_module_globals_table);

} // namespace

// extern "C": genhdr/moduledefs.h declares this with C linkage, same
// reasoning as imu_module/camera_module's own tail comments.
extern "C" const mp_obj_module_t android_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &android_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_android, android_module);
