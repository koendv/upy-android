// upy-android: android.location, a thin layer over Android's
// LocationManager (LocationShim.kt, via location_jni_bridge.cpp).
//
//   android.location.start(interval_ms=1000, min_distance_m=0)
//                                             begin updates (GPS, network);
//                                             no new fix until moved
//                                             min_distance_m
//   android.location.read(timeout_ms=0)       fix new since the last read(),
//                                             waiting up to timeout_ms
//                                             (-1: until one arrives);
//                                             None if there is none
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
#include <ctime>

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

// Sequence number of the fix read() last returned.
long long g_read_seq = 0;

mp_obj_t read_fix(bool last_known) {
    double fix[8];
    char provider[32];
    if (!location_bridge_read(last_known, fix, provider, sizeof(provider))) {
        return mp_const_none;
    }
    if (!last_known) {
        long long seq = (long long) fix[7];
        if (seq == g_read_seq) {
            return mp_const_none;   // already returned by an earlier read()
        }
        g_read_seq = seq;
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
    enum { ARG_interval_ms, ARG_min_distance_m };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_interval_ms, MP_ARG_INT, {.u_int = 1000}},
        {MP_QSTR_min_distance_m, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(0)}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    float min_distance_m = (float) mp_obj_get_float(args[ARG_min_distance_m].u_obj);
    if (min_distance_m < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("min_distance_m must be >= 0"));
    }
    switch (location_bridge_start(args[ARG_interval_ms].u_int, min_distance_m)) {
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

long now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long) ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

// Waits in 5 ms steps, like camera_module.cpp's wait_for_frame(), so
// Interrupt stays responsive; a new fix wakes it at once. Only an atomic
// sequence number is checked while waiting; the fix itself is fetched
// over JNI once it changed.
mp_obj_t location_read(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_timeout_ms };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_timeout_ms, MP_ARG_INT, {.u_int = 0}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    mp_int_t timeout_ms = args[ARG_timeout_ms].u_int;

    long start = now_ms();
    while (true) {
        long long seq = location_bridge_seq();
        if (seq != g_read_seq) {
            mp_obj_t fix = read_fix(false);
            if (fix != mp_const_none) {
                return fix;
            }
            // No fix to fetch (stopped since): don't fetch again for it.
            g_read_seq = seq;
        }
        if (timeout_ms >= 0 && now_ms() - start >= timeout_ms) {
            return mp_const_none;
        }
        location_bridge_wait(5);
        mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_KW(location_read_obj, 0, location_read);

mp_obj_t location_last() {
    return read_fix(true);
}
static MP_DEFINE_CONST_FUN_OBJ_0(location_last_obj, location_last);

// Runtime-queryable usage reference for an AI (or human) driving this
// module blind over adb, with no repo access -- see
// AdbExecProvider.kt/adb_help.yaml (help -> help('modules') -> import
// android; print(android.location.help())).
const char location_help_text[] =
    "module: android.location (import android; android.location.*)\n"
    "a thin wrapper over Android's LocationManager (GPS/network provider)\n"
    "methods:\n"
    "  start(interval_ms=1000, min_distance_m=0): begin location updates\n"
    "    OSError(EACCES) if location permission not granted (a prompt appears on the phone; run again after granting)\n"
    "    OSError(ENODEV) if no location provider is enabled in Android settings\n"
    "  stop(): end updates (also done implicitly by reset())\n"
    "  read(timeout_ms=0): the fix that's new since the last read() call\n"
    "    timeout_ms=0: non-blocking, None if nothing new\n"
    "    timeout_ms>0: wait up to that many ms\n"
    "    timeout_ms=-1: wait indefinitely (interruptible)\n"
    "  last(): Android's last known fix regardless of read() history, or None\n"
    "fix_format: (latitude, longitude, altitude_m, accuracy_m, speed_mps, bearing_deg, time_ms, provider)\n"
    "  values Android does not have for this fix are None (latitude, longitude, time_ms and provider are always present)\n"
    "see_also: android.help()\n"
;
mp_obj_t location_help() {
    return mp_obj_new_str(location_help_text, strlen(location_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(location_help_obj, location_help);

const mp_rom_map_elem_t location_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_location)},
    {MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&location_help_obj)},
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
