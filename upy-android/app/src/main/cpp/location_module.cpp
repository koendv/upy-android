// upy-android: android.location, a thin layer over Android's
// LocationManager (LocationShim.kt, via location_jni_bridge.cpp).
//
//   android.location.start(interval_ms=1000)  begin updates (GPS, network)
//   android.location.read()                   latest fix or None
//   android.location.last()                   Android's last known fix or None
//   android.location.stop()                   end updates (also done by reset)
//
// A fix is (latitude, longitude, altitude_m, accuracy_m, speed_mps,
// bearing_deg, time_ms, provider), None for values Android does not have.
extern "C" {
#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"
}

#include <cmath>
#include <cstring>

#include "location_jni_bridge.h"
#include "location_module.h"

namespace {

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

mp_obj_t float_or_none(double value) {
    return std::isnan(value) ? mp_const_none : mp_obj_new_float(value);
}

mp_obj_t read_fix(bool last_known) {
    double fix[7];
    char provider[32];
    if (!location_bridge_read(last_known, fix, provider, sizeof(provider))) {
        return mp_const_none;
    }
    mp_obj_t items[8] = {
        mp_obj_new_float(fix[0]),
        mp_obj_new_float(fix[1]),
        float_or_none(fix[2]),
        float_or_none(fix[3]),
        float_or_none(fix[4]),
        float_or_none(fix[5]),
        mp_obj_new_int_from_ll((long long) fix[6]),
        mp_obj_new_str(provider, strlen(provider)),
    };
    return mp_obj_new_tuple(8, items);
}

mp_obj_t location_start(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_interval_ms };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_interval_ms, MP_ARG_INT, {.u_int = 1000}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    switch (location_bridge_start(args[ARG_interval_ms].u_int)) {
        case LOCATION_OK:
            break;
        case LOCATION_NO_PERMISSION:
            raise_os_error(MP_EACCES, "location permission not granted: allow it in the prompt, then run again");
            break;
        default:
            raise_os_error(MP_ENODEV, "no location provider enabled: turn on location in Android settings");
            break;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(location_start_obj, 0, location_start);

mp_obj_t location_stop() {
    location_bridge_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(location_stop_obj, location_stop);

mp_obj_t location_read() {
    return read_fix(false);
}
static MP_DEFINE_CONST_FUN_OBJ_0(location_read_obj, location_read);

mp_obj_t location_last() {
    return read_fix(true);
}
static MP_DEFINE_CONST_FUN_OBJ_0(location_last_obj, location_last);

const mp_rom_map_elem_t location_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_location)},
    {MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&location_start_obj)},
    {MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&location_stop_obj)},
    {MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&location_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_last), MP_ROM_PTR(&location_last_obj)},
};
MP_DEFINE_CONST_DICT(location_globals, location_globals_table);

}  // namespace

extern "C" const mp_obj_module_t location_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &location_globals,
};

extern "C" void location_bridge_init(void *jni_env) {
    location_bridge_init_impl(jni_env);
}
