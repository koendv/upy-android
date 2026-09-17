// upy-android native imu module -- OUR OWN bridge between MicroPython
// and Android's NDK sensor API (ASensorManager/ASensorEventQueue),
// deliberately shaped to match upstream OpenMV's own py_imu.c Python
// API (acceleration_mg()/angular_rate_mdps()/roll()/pitch()/sleep()) so
// scripts written against real OpenMV hardware need no changes. See
// imu_module.h and SESSION_STATE.yaml's py_imu.c entries for the full
// research/design trail this implementation follows.
//
// SCOPE (SESSION_STATE.yaml has the full decision trail): implemented
// exactly acceleration_mg(), angular_rate_mdps(), roll(), pitch(),
// sleep() -- missing-sensor (no accelerometer/gyroscope on this device)
// defaults to (0.0, 0.0, 0.0) / 0.0 / no-op rather than raising, matching
// OpenMV's own py_imu_roll_rotation()/py_imu_pitch_rotation() C-API
// fallback-to-0.0f-when-uninitialized precedent. temperature_c(),
// __write_reg(), and __read_reg() are deliberately NOT implemented at
// all (not even as stubs) -- no faithful Android equivalent exists for
// IMU die temperature, and raw register access is physically impossible
// from an Android app process; a script calling these gets the normal
// MicroPython AttributeError rather than a fabricated-looking value
// (user's own framing: "instead of giving a false answer we do not
// implement those functions").
//
// READ SEMANTICS (user decision, via AskUserQuestion): every getter
// BLOCKS until a genuinely fresh sample arrives (drain-then-wait, see
// wait_for_fresh_event() below), not a cached latest-reading -- closest
// to upstream py_imu.c's own per-call register-read semantics, and
// reuses the same blocking-read shape csi.snapshot() already uses
// (camera_module.cpp) rather than a new pattern.
//
// THREADING: no new thread needed (SESSION_STATE.yaml's threading-design
// finding, corrected after checking the real NDK header rather than
// assumed) -- unlike AImageReader (whose onImageAvailable callback fires
// on an Android-owned delivery thread, the actual reason camera_module.cpp
// needs a semaphore bridge), ASensorManager_createEventQueue() takes a
// CALLER-supplied ALooper*, so the single reused MicroPython worker
// thread (see engine_jni.cpp's threading contract) can prepare its own
// looper once and call ALooper_pollOnce() directly. Confirmed against
// real working code, not just the header: Google's own ndk-samples
// sensor-graph example (~/src/ndk-samples/sensor-graph/.../sensorgraph.cpp)
// uses exactly this shape. This also means no interrupt-wake hook is
// needed (unlike camera_interrupt_active_wait()) -- ALooper_pollOnce()'s
// own timeout parameter already bounds interrupt-check latency to
// kWaitChunkMs per iteration, the same responsiveness camera_module.cpp
// gets from its semaphore post, achieved here for free.

#include "imu_module.h"

#include <android/sensor.h>
#include <android/looper.h>
#include <cmath>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/nlr.h"
#include "py/mperrno.h"
}

namespace {

// Same 5ms interrupt-check granularity camera_module.cpp's own wait loop
// uses (mp_hal_delay_ms()'s MP_HAL_DELAY_CHUNK_MS) -- reused so
// interrupt responsiveness stays uniform across every blocking call in
// the app, not reinvented for this one.
constexpr int kWaitChunkMs = 5;
// Generous bound once a sensor is confirmed present -- a genuine timeout
// here means a real malfunction (raised as OSError below), not "sensor
// missing" (that's decided before this is ever reached, see
// ensure_*_enabled()). Same value as camera_module.cpp's own
// kSnapshotTimeoutMs for consistency, though the underlying reasoning
// differs (that one absorbs Camera2's documented 3A-convergence latency;
// this one is just a generous margin -- a 52Hz-configured sensor should
// never legitimately take anywhere near this long).
constexpr int kReadTimeoutMs = 500;
// ~52Hz, matching OpenMV's own XL_ODR_52Hz/GY_ODR_52Hz default rate
// (modules/py_imu.c's py_imu_init(), read directly from upstream).
constexpr int32_t kSampleRatePeriodUs = 1000000 / 52;
// Arbitrary caller-chosen identifier distinguishing this queue's fd
// source in ALooper_pollOnce()'s return value -- nothing else shares
// this looper, so any small constant works (google's own sensor-graph
// sample uses LOOPER_ID_USER=3 for the same reason).
constexpr int kLooperIdent = 1;
constexpr float kMetersPerSecondSquaredToMg = 1000.0f / 9.80665f;
constexpr float kRadToDeg = 180.0f / (float) M_PI;

// ASensorManager_getInstanceForPackage() needs the real package name --
// hardcoded rather than queried via JNI/Context (the ndk-samples
// reference code does that, but only to support running as a shared
// library loaded by arbitrary host apps; this project's applicationId
// is fixed, see app/build.gradle.kts, so a JNIEnv/Context round-trip
// would be real added complexity for no practical benefit). No pre-26
// dlopen/dlsym fallback either (both ndk-samples reference files have
// one) -- this project's minSdk is 26, exactly
// ASensorManager_getInstanceForPackage()'s own __INTRODUCED_IN(26), so
// the deprecated no-arg ASensorManager_getInstance() is never needed.
constexpr char kPackageName[] = "eu.kdvelectronics.upyandroid";

struct ImuState {
    ASensorManager *manager;
    ALooper *looper;
    ASensorEventQueue *queue;
    const ASensor *accel; // NULL if this device has none -- a fixed hardware fact, queried once
    const ASensor *gyro;  // NULL if this device has none -- a fixed hardware fact, queried once
    bool queried_capabilities;
    bool accel_enabled;
    bool gyro_enabled;
};

ImuState g_imu = {nullptr, nullptr, nullptr, nullptr, nullptr, false, false, false};

// Matches camera_module.cpp's own raise_os_error() shape (a real two-arg
// OSError(errno, message), not just a bare errno) -- duplicated rather
// than shared, same small-helper-per-module style camera_module.cpp
// itself already established.
void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// Queries the manager + both sensor capability pointers exactly once,
// ever -- these are fixed hardware facts (does this device have an
// accelerometer/gyroscope at all), not open resources, so unlike the
// event queue below they are never torn down by imu_close_all().
void ensure_capabilities_queried() {
    if (g_imu.queried_capabilities) {
        return;
    }
    g_imu.manager = ASensorManager_getInstanceForPackage(kPackageName);
    if (g_imu.manager) {
        g_imu.accel = ASensorManager_getDefaultSensor(g_imu.manager, ASENSOR_TYPE_ACCELEROMETER);
        g_imu.gyro = ASensorManager_getDefaultSensor(g_imu.manager, ASENSOR_TYPE_GYROSCOPE);
    }
    g_imu.queried_capabilities = true;
}

// (Re)creates the event queue if it isn't already open -- torn down by
// imu_close_all(), rebuilt lazily here on next use, same lazy-rebuild
// shape as camera_module.cpp's ensure_session(). The looper itself is
// prepared at most once ever (thread-lifetime-scoped, the single reused
// MicroPython worker thread lives for the whole process) and is never
// torn down by imu_close_all() -- there is no NDK "release this thread's
// looper" call that would even be appropriate here.
void ensure_queue() {
    ensure_capabilities_queried();
    if (g_imu.queue || !g_imu.manager) {
        return;
    }
    if (!g_imu.looper) {
        g_imu.looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    }
    g_imu.queue = ASensorManager_createEventQueue(g_imu.manager, g_imu.looper, kLooperIdent, nullptr, nullptr);
}

void ensure_accel_enabled() {
    ensure_queue();
    if (!g_imu.accel || g_imu.accel_enabled || !g_imu.queue) {
        return;
    }
    ASensorEventQueue_enableSensor(g_imu.queue, g_imu.accel);
    ASensorEventQueue_setEventRate(g_imu.queue, g_imu.accel, kSampleRatePeriodUs);
    g_imu.accel_enabled = true;
}

void ensure_gyro_enabled() {
    ensure_queue();
    if (!g_imu.gyro || g_imu.gyro_enabled || !g_imu.queue) {
        return;
    }
    ASensorEventQueue_enableSensor(g_imu.queue, g_imu.gyro);
    ASensorEventQueue_setEventRate(g_imu.queue, g_imu.gyro, kSampleRatePeriodUs);
    g_imu.gyro_enabled = true;
}

// Tears down the event queue (disabling any enabled sensors first),
// leaving accel/gyro_enabled false and queue null -- the next
// ensure_accel_enabled()/ensure_gyro_enabled() call rebuilds everything
// from scratch via ensure_queue(). Shared by imu_close_all() and
// wait_for_fresh_event()'s timeout path below (self-heal, see there).
void teardown_queue() {
    if (g_imu.queue) {
        if (g_imu.accel_enabled) {
            ASensorEventQueue_disableSensor(g_imu.queue, g_imu.accel);
        }
        if (g_imu.gyro_enabled) {
            ASensorEventQueue_disableSensor(g_imu.queue, g_imu.gyro);
        }
        ASensorManager_destroyEventQueue(g_imu.manager, g_imu.queue);
        g_imu.queue = nullptr;
    }
    g_imu.accel_enabled = false;
    g_imu.gyro_enabled = false;
}

// Drains any events already queued (so a long gap between Python calls
// can't return a stale reading -- the sensor keeps streaming in the
// background between calls once enabled), then blocks in kWaitChunkMs
// increments -- same granularity/interrupt-check shape as
// camera_module.cpp's wait_and_acquire_frame() -- until a genuinely NEW
// event of `want_type` arrives. A differently-typed event (e.g. a gyro
// sample arriving while waiting for accel, both enabled on this one
// shared queue) is discarded and waiting continues. Returns false if
// kReadTimeoutMs elapses with no matching event -- the caller raises
// this as OSError, a real malfunction once a sensor is confirmed
// present, NOT the same case as "sensor missing" (decided by the
// caller before this is ever reached).
bool wait_for_fresh_event(int32_t want_type, ASensorEvent *out) {
    ASensorEvent event;
    while (ASensorEventQueue_getEvents(g_imu.queue, &event, 1) > 0) {
        // discard stale queue contents
    }
    for (int waited = 0; waited < kReadTimeoutMs; waited += kWaitChunkMs) {
        ALooper_pollOnce(kWaitChunkMs, nullptr, nullptr, nullptr);
        // Drain the FULL batch available right now, keeping only the
        // LAST matching sample -- a suspended/batched non-wakeup sensor
        // (e.g. after the screen was off, confirmed via dumpsys
        // sensorservice: this device's accelerometer/gyroscope are both
        // "Non-wakeup") can flush several queued-up historical events at
        // once when delivery resumes. Returning on the FIRST match found
        // in such a flush (the original bug here) could silently hand
        // back a stale pre-gap sample instead of the genuinely newest
        // one -- caught on-device: readings stayed suspiciously constant
        // across real physical reorientation until an explicit timeout
        // surfaced the underlying suspend/resume gap.
        bool got_one = false;
        while (ASensorEventQueue_getEvents(g_imu.queue, &event, 1) > 0) {
            if (event.type == want_type) {
                *out = event;
                got_one = true;
            }
        }
        if (got_one) {
            return true;
        }
        // Raises (nlr_jump) if the user tapped Interrupt while we were
        // waiting -- same mechanism camera_module.cpp's wait loop uses.
        mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
    }
    // Self-heal rather than stay silently wedged: without this, once one
    // read times out, every subsequent call's ensure_*_enabled() would
    // see accel_enabled/gyro_enabled already true and never retry
    // actually re-enabling anything, even though the real underlying
    // subscription may now be dead -- caught on-device (2026-09-16):
    // back-to-back reads kept timing out identically after the first
    // failure, only recovering after a full app restart rebuilt
    // everything from scratch. Tearing down the queue here makes the
    // NEXT call rebuild it via ensure_queue() instead of requiring a
    // full process restart to recover. (Root cause of the timeouts
    // themselves was separately traced to :engine's process priority --
    // see BoardManager.kt's BIND_ABOVE_CLIENT change -- this is a
    // defensive recovery path, not a fix for that root cause.)
    teardown_queue();
    return false;
}

mp_obj_t imu_tuple(float x, float y, float z) {
    return mp_obj_new_tuple(3, (mp_obj_t[3]) {
        mp_obj_new_float(x), mp_obj_new_float(y), mp_obj_new_float(z)
    });
}

// Missing-accelerometer default: (0.0, 0.0, 0.0), per SESSION_STATE.yaml's
// missing-sensor-default decision -- not every Android device is
// guaranteed to have one, and this is honestly-absent data, not a
// dangerous half-correct value (see that entry's auto_gain()/
// auto_whitebal() contrast).
mp_obj_t imu_acceleration_mg() {
    ensure_accel_enabled();
    if (!g_imu.accel) {
        return imu_tuple(0.0f, 0.0f, 0.0f);
    }
    ASensorEvent event;
    if (!wait_for_fresh_event(ASENSOR_TYPE_ACCELEROMETER, &event)) {
        raise_os_error(MP_ETIMEDOUT, "imu: accelerometer read timed out");
    }
    return imu_tuple(event.acceleration.x * kMetersPerSecondSquaredToMg,
                     event.acceleration.y * kMetersPerSecondSquaredToMg,
                     event.acceleration.z * kMetersPerSecondSquaredToMg);
}
static MP_DEFINE_CONST_FUN_OBJ_0(imu_acceleration_mg_obj, imu_acceleration_mg);

// Missing-gyroscope default: (0.0, 0.0, 0.0) -- same reasoning as
// acceleration_mg() above; a real, common case (many phones lack a
// gyroscope entirely), not a hypothetical.
mp_obj_t imu_angular_rate_mdps() {
    ensure_gyro_enabled();
    if (!g_imu.gyro) {
        return imu_tuple(0.0f, 0.0f, 0.0f);
    }
    ASensorEvent event;
    if (!wait_for_fresh_event(ASENSOR_TYPE_GYROSCOPE, &event)) {
        raise_os_error(MP_ETIMEDOUT, "imu: gyroscope read timed out");
    }
    // rad/s -> mdps: deg/s = rad/s * (180/pi); mdps = deg/s * 1000
    return imu_tuple(event.gyro.x * kRadToDeg * 1000.0f,
                     event.gyro.y * kRadToDeg * 1000.0f,
                     event.gyro.z * kRadToDeg * 1000.0f);
}
static MP_DEFINE_CONST_FUN_OBJ_0(imu_angular_rate_mdps_obj, imu_angular_rate_mdps);

// roll()/pitch() trig ported near-verbatim from upstream py_imu.c's
// py_imu_get_roll()/py_imu_get_pitch() (modules/py_imu.c, read directly),
// simplified: OpenMV's own fast_atan2f() fixed-point approximation
// (optimized for MCUs without FPU-friendly libm) is dropped in favor of
// plain atan2f() from <cmath> -- a real Android device has a proper FPU
// and libm, no reason to reimplement a lower-accuracy approximation.
// atan2 is scale-invariant, so raw m/s^2 is used directly rather than
// first converting to mg (the conversion factor cancels out). OpenMV's
// OMV_IMU_X_Y_ROTATION_DEGREES/OMV_IMU_MOUNTING_Z_DIRECTION board-mounting-
// orientation constants are per-BOARD calibration knobs with no
// principled per-phone equivalent (varies by device, no way to know a
// given phone's chip mounting from software) -- this implementation uses
// the plain/untransformed case (equivalent to
// OMV_IMU_X_Y_ROTATION_DEGREES=0, OMV_IMU_MOUNTING_Z_DIRECTION=-1),
// upstream's own default. fmodf()'s C semantics (can return a negative
// result, unlike Python's %) are kept as-is, faithfully matching
// upstream's own formula rather than "fixing" a mismatch that may not
// even be a bug.
mp_obj_t imu_roll() {
    ensure_accel_enabled();
    if (!g_imu.accel) {
        return mp_obj_new_float(0.0f); // matches OpenMV's own py_imu_roll_rotation() !imu_initialized fallback
    }
    ASensorEvent event;
    if (!wait_for_fresh_event(ASENSOR_TYPE_ACCELEROMETER, &event)) {
        raise_os_error(MP_ETIMEDOUT, "imu: accelerometer read timed out");
    }
    float x = event.acceleration.x;
    float y = event.acceleration.y;
    float roll = fmodf((atan2f(-x, y) * kRadToDeg) + 180.0f, 360.0f);
    return mp_obj_new_float(roll);
}
static MP_DEFINE_CONST_FUN_OBJ_0(imu_roll_obj, imu_roll);

mp_obj_t imu_pitch() {
    ensure_accel_enabled();
    if (!g_imu.accel) {
        return mp_obj_new_float(0.0f); // matches OpenMV's own py_imu_pitch_rotation() !imu_initialized fallback
    }
    ASensorEvent event;
    if (!wait_for_fresh_event(ASENSOR_TYPE_ACCELEROMETER, &event)) {
        raise_os_error(MP_ETIMEDOUT, "imu: accelerometer read timed out");
    }
    float y = event.acceleration.y;
    float z = event.acceleration.z;
    float pitch = atan2f(z, -y) * kRadToDeg;
    return mp_obj_new_float(pitch);
}
static MP_DEFINE_CONST_FUN_OBJ_0(imu_pitch_obj, imu_pitch);

// enable=True (matches upstream py_imu_sleep()'s own polarity: sleep(1)
// stops sampling) disables whichever sensors are currently enabled;
// enable=False re-enables whichever sensors are actually present --
// no-op, no crash, if a given sensor doesn't exist (missing-sensor
// default applies here too, via the ensure_*_enabled() guards).
mp_obj_t imu_sleep(mp_obj_t enable_in) {
    bool en = mp_obj_is_true(enable_in);
    if (en) {
        if (g_imu.queue && g_imu.accel_enabled) {
            ASensorEventQueue_disableSensor(g_imu.queue, g_imu.accel);
            g_imu.accel_enabled = false;
        }
        if (g_imu.queue && g_imu.gyro_enabled) {
            ASensorEventQueue_disableSensor(g_imu.queue, g_imu.gyro);
            g_imu.gyro_enabled = false;
        }
    } else {
        ensure_accel_enabled();
        ensure_gyro_enabled();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(imu_sleep_obj, imu_sleep);

// Flat module, no type/instance (imu.acceleration_mg(), not
// imu.IMU().acceleration_mg()) -- deliberately matches upstream
// py_imu.c's own shape exactly (its globals_dict_table holds the
// function objects directly, no make_new/no mp_obj_type_t at all,
// confirmed by reading the real file), not csi_module's/display_module's
// per-instance-object shape.
const mp_rom_map_elem_t imu_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_imu)},
    {MP_ROM_QSTR(MP_QSTR_acceleration_mg), MP_ROM_PTR(&imu_acceleration_mg_obj)},
    {MP_ROM_QSTR(MP_QSTR_angular_rate_mdps), MP_ROM_PTR(&imu_angular_rate_mdps_obj)},
    {MP_ROM_QSTR(MP_QSTR_roll), MP_ROM_PTR(&imu_roll_obj)},
    {MP_ROM_QSTR(MP_QSTR_pitch), MP_ROM_PTR(&imu_pitch_obj)},
    {MP_ROM_QSTR(MP_QSTR_sleep), MP_ROM_PTR(&imu_sleep_obj)},
};
MP_DEFINE_CONST_DICT(imu_module_globals, imu_module_globals_table);

} // namespace

// extern "C" (not inside the anonymous namespace above, unlike
// everything else in this file) -- same reasoning as csi_module in
// camera_module.cpp: genhdr/moduledefs.h declares
// `extern const struct _mp_obj_module_t imu_module;` with C linkage.
extern "C" const mp_obj_module_t imu_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &imu_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_imu, imu_module);

extern "C" void imu_close_all(void) {
    teardown_queue();
    // manager/accel/gyro/looper intentionally left set -- see
    // imu_module.h's comment and ensure_capabilities_queried()/
    // ensure_queue() above.
}
