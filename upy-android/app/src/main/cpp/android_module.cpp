// upy-android native android module.
// micropython android port only.
// see session-state: android_module.cpp#module_design

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
}

extern "C" const mp_obj_module_t imu_module;

extern const mp_obj_fun_builtin_fixed_t android_proximity_distance_cm_obj;
extern const mp_obj_fun_builtin_fixed_t android_light_on_obj;
extern const mp_obj_fun_builtin_fixed_t android_light_off_obj;
extern const mp_obj_fun_builtin_fixed_t android_zoom_set_obj;
extern const mp_obj_fun_builtin_fixed_t android_zoom_range_obj;
extern const mp_obj_fun_builtin_fixed_t android_settings_obj;

extern "C" const mp_obj_module_t mediastore_module;
extern "C" const mp_obj_module_t fileprovider_module;

namespace {

const mp_rom_map_elem_t android_proximity_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_proximity)},
    {MP_ROM_QSTR(MP_QSTR_distance_cm), MP_ROM_PTR(&android_proximity_distance_cm_obj)},
};
MP_DEFINE_CONST_DICT(android_proximity_globals, android_proximity_globals_table);

const mp_rom_map_elem_t android_light_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_light)},
    {MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&android_light_on_obj)},
    {MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&android_light_off_obj)},
};
MP_DEFINE_CONST_DICT(android_light_globals, android_light_globals_table);

const mp_rom_map_elem_t android_zoom_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_zoom)},
    {MP_ROM_QSTR(MP_QSTR_set), MP_ROM_PTR(&android_zoom_set_obj)},
    {MP_ROM_QSTR(MP_QSTR_range), MP_ROM_PTR(&android_zoom_range_obj)},
};
MP_DEFINE_CONST_DICT(android_zoom_globals, android_zoom_globals_table);

// Plain mp_obj_module_t, same as imu_module.
// see session-state: android_module.cpp#module_design
const mp_obj_module_t android_proximity_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &android_proximity_globals,
};
const mp_obj_module_t android_light_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &android_light_globals,
};
const mp_obj_module_t android_zoom_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &android_zoom_globals,
};

const mp_rom_map_elem_t android_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_android)},
    {MP_ROM_QSTR(MP_QSTR_imu), MP_ROM_PTR(&imu_module)},
    {MP_ROM_QSTR(MP_QSTR_proximity), MP_ROM_PTR(&android_proximity_module)},
    {MP_ROM_QSTR(MP_QSTR_light), MP_ROM_PTR(&android_light_module)},
    {MP_ROM_QSTR(MP_QSTR_zoom), MP_ROM_PTR(&android_zoom_module)},
    {MP_ROM_QSTR(MP_QSTR_settings), MP_ROM_PTR(&android_settings_obj)},
    {MP_ROM_QSTR(MP_QSTR_mediastore), MP_ROM_PTR(&mediastore_module)},
    {MP_ROM_QSTR(MP_QSTR_fileprovider), MP_ROM_PTR(&fileprovider_module)},
};
MP_DEFINE_CONST_DICT(android_module_globals, android_module_globals_table);

} // namespace

// extern "C": genhdr/moduledefs.h declares this with C linkage, same
// reasoning as imu_module/csi_module's own tail comments.
extern "C" const mp_obj_module_t android_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &android_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_android, android_module);
