// upy-android native android module -- OUR OWN code, NOT vendored OpenMV
// source. Home for phone-native capabilities that have no real OpenMV
// module name to preserve compatibility with (unlike csi/display, which
// are genuine upstream OpenMV module names): imu, proximity, light,
// touch. See SESSION_STATE.yaml's "android module" design discussion
// for the full research/naming trail behind this module existing at
// all, and why imu (a real, already-shipped top-level module) moved
// under it despite the rename/relocation cost -- user's own call, "keep
// imu name" (the Python-visible name stays imu.*, only its import path
// changes from `import imu` to `import android` + `android.imu.*`).
//
// imu, proximity, light, and zoom are nested here as genuine
// mp_obj_module_t sub-objects (the SAME struct type/shape a real
// top-level module uses), not flattened into
// android.imu_acceleration_mg()-style names. touch dropped for this
// round (see SESSION_STATE.yaml).

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
}

// Declared extern, not redefined here -- imu_module.cpp still owns the
// real struct (its own function objects, globals table, close_all()
// hook); this file only references it to nest it under android.
extern "C" const mp_obj_module_t imu_module;

// android.proximity.distance_cm() -- also implemented in
// imu_module.cpp (shares its ASensorManager/queue/looper state with
// imu, see that file's own comments), but exposed here as its own
// android.proximity namespace, not folded into imu.*. Ordinary C++
// extern (not extern "C") is enough -- this is a plain function object
// referenced from one other .cpp file in the same build, not a
// moduledefs.h-registered top-level module.
extern const mp_obj_fun_builtin_fixed_t android_proximity_distance_cm_obj;

// android.light.on()/off() and android.zoom.set()/range() -- both
// implemented in camera_module.cpp (tied to the active csi capture
// session, share g_cam with csi.CSI() -- see that file's own
// comments), exposed here as their own android.light/android.zoom
// namespaces. Same ordinary-C++-extern reasoning as proximity above.
extern const mp_obj_fun_builtin_fixed_t android_light_on_obj;
extern const mp_obj_fun_builtin_fixed_t android_light_off_obj;
extern const mp_obj_fun_builtin_fixed_t android_zoom_set_obj;
extern const mp_obj_fun_builtin_fixed_t android_zoom_range_obj;

// android.tf.version() -- implemented in tf_module.cpp (see that file's
// own header comment: a MOCKUP proving the LiteRT CMake wiring actually
// links and runs on-device, not the real android.tf API surface yet).
// Same ordinary-C++-extern reasoning as proximity/light/zoom above.
extern const mp_obj_fun_builtin_fixed_t android_tf_version_obj;

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

// android.tf -- MOCKUP, one function only (version()), see tf_module.cpp's
// own header comment. Same nested-submodule shape as proximity/light/zoom
// above, not the real android.tf API surface yet.
const mp_rom_map_elem_t android_tf_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_tf)},
    {MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&android_tf_version_obj)},
};
MP_DEFINE_CONST_DICT(android_tf_globals, android_tf_globals_table);

// Plain mp_obj_module_t, same as imu_module -- none of these are
// registered via MP_REGISTER_MODULE (no top-level `import proximity`/
// `import light`/`import zoom`/`import tf`), only ever reached as
// android.*.
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
const mp_obj_module_t android_tf_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &android_tf_globals,
};

const mp_rom_map_elem_t android_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_android)},
    {MP_ROM_QSTR(MP_QSTR_imu), MP_ROM_PTR(&imu_module)},
    {MP_ROM_QSTR(MP_QSTR_proximity), MP_ROM_PTR(&android_proximity_module)},
    {MP_ROM_QSTR(MP_QSTR_light), MP_ROM_PTR(&android_light_module)},
    {MP_ROM_QSTR(MP_QSTR_zoom), MP_ROM_PTR(&android_zoom_module)},
    {MP_ROM_QSTR(MP_QSTR_tf), MP_ROM_PTR(&android_tf_module)},
};
MP_DEFINE_CONST_DICT(android_module_globals, android_module_globals_table);

} // namespace

// extern "C", same reasoning as imu_module/csi_module's own tail
// comments: genhdr/moduledefs.h declares this with C linkage.
extern "C" const mp_obj_module_t android_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &android_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_android, android_module);
