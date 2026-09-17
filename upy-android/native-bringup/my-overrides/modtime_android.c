// upy-android: real Android time-of-day support for the `time` module.
//
// Textually #include'd into extmod/modtime.c via MICROPY_PY_TIME_INCLUDEFILE
// (mpconfigport.h) -- same mechanism ports/unix/modtime.c and
// ports/esp32/modtime.c use, so these are static functions sharing
// modtime.c's translation unit, not a separately linked file.
//
// Deliberately mirrors ports/unix's approach (real libc gmtime_r/
// localtime_r/mktime, MICROPY_PY_TIME_EXTRA_GLOBALS), not the esp32/rp2
// style of relying on extmod/modtime.c's generic
// MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME path -- that generic path
// registers gmtime() and localtime() as literal aliases of the same
// function, which cannot give genuine UTC vs. device-local distinction.
// Android has a real timezone database (Bionic's tzset()/localtime_r()),
// so unlike ports with no OS tz database, faking localtime()==UTC would
// throw away real, available correctness.

#include <time.h>

#include "shared/timeutils/timeutils.h"
// Clock (time.clock()) -- OpenMV script compatibility (find_apriltags.py,
// lcd_shield.py, single_color_rgb565_blob_tracking.py all do
// `clock = time.clock()` once, then clock.tick()/clock.fps()). Genuinely
// standalone (py_clock.c has no MP_REGISTER_MODULE of its own -- OpenMV's
// own fork wires it into `time` in code this project doesn't vendor, see
// SESSION_STATE.yaml), so it rides the exact same MICROPY_PY_TIME_EXTRA_
// GLOBALS hook below as gmtime/localtime/mktime, not a separate mechanism.
#include "py_clock.h"

// time.time() / time.time_ns()'s time() half: real seconds since the Unix
// epoch (MICROPY_EPOCH_IS_1970, mpconfigport.h) with sub-second precision
// as a float -- MICROPY_FLOAT_IMPL_DOUBLE is already on for math, so this
// is free precision matching CPython's own time.time() semantics, not an
// extra cost.
static mp_obj_t mp_time_time_get(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return mp_obj_new_float((mp_float_t)ts.tv_sec + (mp_float_t)ts.tv_nsec / MICROPY_FLOAT_CONST(1e9));
}

// Shared body for gmtime()/localtime(): only the reentrant conversion
// function pointer (gmtime_r vs localtime_r) differs between them.
static mp_obj_t mod_time_gm_local_time(size_t n_args, const mp_obj_t *args,
    struct tm *(*time_func)(const time_t *restrict, struct tm *restrict)) {
    time_t t;
    if (n_args == 0 || args[0] == mp_const_none) {
        t = time(NULL);
    } else {
        t = (time_t)timeutils_obj_get_timestamp(args[0]);
    }
    struct tm tm_result;
    time_func(&t, &tm_result);

    mp_obj_t tuple[8] = {
        mp_obj_new_int(tm_result.tm_year + 1900),
        mp_obj_new_int(tm_result.tm_mon + 1),
        mp_obj_new_int(tm_result.tm_mday),
        mp_obj_new_int(tm_result.tm_hour),
        mp_obj_new_int(tm_result.tm_min),
        mp_obj_new_int(tm_result.tm_sec),
        // struct tm: tm_wday is 0=Sunday..6=Saturday; MicroPython's
        // convention (like CPython's time module) is 0=Monday..6=Sunday.
        mp_obj_new_int((tm_result.tm_wday + 6) % 7),
        mp_obj_new_int(tm_result.tm_yday + 1),
    };
    return mp_obj_new_tuple(8, tuple);
}

// gmtime(): real UTC, via gmtime_r (reentrant/thread-safe variant).
static mp_obj_t mod_time_gmtime(size_t n_args, const mp_obj_t *args) {
    return mod_time_gm_local_time(n_args, args, gmtime_r);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_time_gmtime_obj, 0, 1, mod_time_gmtime);

// localtime(): genuine Android device-local time (timezone + DST via
// Bionic's tz database), via localtime_r.
static mp_obj_t mod_time_localtime(size_t n_args, const mp_obj_t *args) {
    return mod_time_gm_local_time(n_args, args, localtime_r);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_time_localtime_obj, 0, 1, mod_time_localtime);

// mktime(): inverse of localtime() (matches CPython semantics -- mktime
// interprets its tuple as LOCAL time, not UTC), via real libc mktime().
static mp_obj_t mod_time_mktime(mp_obj_t tuple_in) {
    size_t len;
    mp_obj_t *elem;
    mp_obj_get_array(tuple_in, &len, &elem);

    // localtime() produces an 8-tuple; CPython's accepts 8 or 9 (with
    // tm_isdst as the 9th) -- accept both, same as ports/unix.
    if (len < 8 || len > 9) {
        mp_raise_TypeError(MP_ERROR_TEXT("mktime needs a tuple of length 8 or 9"));
    }

    struct tm time_in = {
        .tm_year = mp_obj_get_int(elem[0]) - 1900,
        .tm_mon = mp_obj_get_int(elem[1]) - 1,
        .tm_mday = mp_obj_get_int(elem[2]),
        .tm_hour = mp_obj_get_int(elem[3]),
        .tm_min = mp_obj_get_int(elem[4]),
        .tm_sec = mp_obj_get_int(elem[5]),
        .tm_isdst = (len == 9) ? mp_obj_get_int(elem[8]) : -1, // -1 = auto-detect
    };
    time_t ret = mktime(&time_in);
    if (ret == (time_t)-1) {
        mp_raise_msg(&mp_type_OverflowError, MP_ERROR_TEXT("invalid mktime usage"));
    }
    return timeutils_obj_from_timestamp(ret);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_time_mktime_obj, mod_time_mktime);

// Deliberately NOT using MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (see the
// file header comment) -- these three are added to the module's globals
// table via this extra-globals hook instead, same mechanism ports/unix
// uses for the same reason.
#define MICROPY_PY_TIME_EXTRA_GLOBALS \
    { MP_ROM_QSTR(MP_QSTR_gmtime), MP_ROM_PTR(&mod_time_gmtime_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_localtime), MP_ROM_PTR(&mod_time_localtime_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_mktime), MP_ROM_PTR(&mod_time_mktime_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_clock), MP_ROM_PTR(&py_clock_type) },
