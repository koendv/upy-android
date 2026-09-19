// upy-android native imu module. OUR OWN bridge between MicroPython
// and Android's NDK sensor API, deliberately shaped to match upstream
// OpenMV's own py_imu.c Python API.
// see session-state: imu_module.cpp#module_design

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
// uses, reused for uniform interrupt responsiveness across the app.
constexpr int kWaitChunkMs = 5;
// Generous bound once a sensor is confirmed present. A genuine timeout
// here means a real malfunction (raised as OSError below), not "sensor
// missing" (decided before this is ever reached).
constexpr int kReadTimeoutMs = 500;
// ~52Hz, matching OpenMV's own XL_ODR_52Hz/GY_ODR_52Hz default rate
// (modules/py_imu.c's py_imu_init(), read directly from upstream).
constexpr int32_t kSampleRatePeriodUs = 1000000 / 52;
// Arbitrary caller-chosen identifier distinguishing this queue's fd
// source in ALooper_pollOnce()'s return value. Nothing else shares
// this looper, so any small constant works.
constexpr int kLooperIdent = 1;
constexpr float kMetersPerSecondSquaredToMg = 1000.0f / 9.80665f;
constexpr float kRadToDeg = 180.0f / (float) M_PI;

// Hardcoded rather than queried via JNI/Context. This project's
// applicationId is fixed (app/build.gradle.kts), so a JNIEnv/Context
// round-trip would be real added complexity for no practical benefit.
// No pre-26 dlopen/dlsym fallback either. This project's minSdk is
// 26, exactly ASensorManager_getInstanceForPackage()'s own
// __INTRODUCED_IN(26).
constexpr char kPackageName[] = "eu.kdvelectronics.upyandroid";

// see session-state: imu_module.cpp#ImuState
struct ImuState {
    ASensorManager *manager;
    ALooper *looper;
    ASensorEventQueue *queue;
    const ASensor *accel;     // NULL if this device has none -- a fixed hardware fact, queried once
    const ASensor *gyro;      // NULL if this device has none -- a fixed hardware fact, queried once
    const ASensor *proximity; // NULL if this device has none -- a fixed hardware fact, queried once
    bool queried_capabilities;
    bool accel_enabled;
    bool gyro_enabled;
    bool proximity_enabled;
    bool proximity_has_reading;
    float proximity_last_distance;
};

ImuState g_imu = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, false, false, false, false, 0.0f};

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// Queries the manager + sensor capability pointers exactly once, ever.
// Fixed hardware facts, not open resources, so unlike the event queue
// below they are never torn down by imu_close_all().
void ensure_capabilities_queried() {
    if (g_imu.queried_capabilities) {
        return;
    }
    g_imu.manager = ASensorManager_getInstanceForPackage(kPackageName);
    if (g_imu.manager) {
        g_imu.accel = ASensorManager_getDefaultSensor(g_imu.manager, ASENSOR_TYPE_ACCELEROMETER);
        g_imu.gyro = ASensorManager_getDefaultSensor(g_imu.manager, ASENSOR_TYPE_GYROSCOPE);
        g_imu.proximity = ASensorManager_getDefaultSensor(g_imu.manager, ASENSOR_TYPE_PROXIMITY);
    }
    g_imu.queried_capabilities = true;
}

// (Re)creates the event queue if it isn't already open. The looper
// itself is prepared at most once ever (the single reused MicroPython
// worker thread lives for the whole process) and is never torn down by
// imu_close_all(). There is no NDK "release this thread's looper"
// call that would even be appropriate here.
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

// Proximity is an on-change sensor (confirmed via `adb shell dumpsys
// sensorservice`), unlike accel/gyro's continuous streaming.
// setEventRate() is still safe/meaningful to call (the framework clamps
// to the sensor's own real rate range regardless of what's requested).
void ensure_proximity_enabled() {
    ensure_queue();
    if (!g_imu.proximity || g_imu.proximity_enabled || !g_imu.queue) {
        return;
    }
    ASensorEventQueue_enableSensor(g_imu.queue, g_imu.proximity);
    ASensorEventQueue_setEventRate(g_imu.queue, g_imu.proximity, kSampleRatePeriodUs);
    g_imu.proximity_enabled = true;
}

// Tears down the event queue (disabling any enabled sensors first),
// leaving accel/gyro_enabled false and queue null. The next
// ensure_*_enabled() call rebuilds everything from scratch. Shared by
// imu_close_all() and wait_for_fresh_event()'s timeout path.
void teardown_queue() {
    if (g_imu.queue) {
        if (g_imu.accel_enabled) {
            ASensorEventQueue_disableSensor(g_imu.queue, g_imu.accel);
        }
        if (g_imu.gyro_enabled) {
            ASensorEventQueue_disableSensor(g_imu.queue, g_imu.gyro);
        }
        if (g_imu.proximity_enabled) {
            ASensorEventQueue_disableSensor(g_imu.queue, g_imu.proximity);
        }
        ASensorManager_destroyEventQueue(g_imu.manager, g_imu.queue);
        g_imu.queue = nullptr;
    }
    g_imu.accel_enabled = false;
    g_imu.gyro_enabled = false;
    g_imu.proximity_enabled = false;
    // The cached reading is only valid for the queue/subscription it
    // came from. A fresh queue means a fresh first read is needed.
    g_imu.proximity_has_reading = false;
}

// see session-state: imu_module.cpp#wait_for_fresh_event
bool wait_for_fresh_event(int32_t want_type, ASensorEvent *out) {
    ASensorEvent event;
    while (ASensorEventQueue_getEvents(g_imu.queue, &event, 1) > 0) {
        // discard stale queue contents
    }
    for (int waited = 0; waited < kReadTimeoutMs; waited += kWaitChunkMs) {
        ALooper_pollOnce(kWaitChunkMs, nullptr, nullptr, nullptr);
        // Drain the FULL batch available right now, keeping only the LAST
        // matching sample.
        // see session-state: imu_module.cpp#wait_for_fresh_event
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
        // waiting. Same mechanism camera_module.cpp's wait loop uses.
        mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
    }
    // Self-heal on timeout rather than stay silently wedged.
    // see session-state: imu_module.cpp#wait_for_fresh_event
    teardown_queue();
    return false;
}

mp_obj_t imu_tuple(float x, float y, float z) {
    return mp_obj_new_tuple(3, (mp_obj_t[3]) {
        mp_obj_new_float(x), mp_obj_new_float(y), mp_obj_new_float(z)
    });
}

// Missing-accelerometer default: (0.0, 0.0, 0.0). Not every Android
// device is guaranteed to have one, and this is honestly-absent data,
// not a dangerous half-correct value.
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

// Missing-gyroscope default: (0.0, 0.0, 0.0). Same reasoning as
// acceleration_mg() above. A real, common case (many phones lack a
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

// see session-state: imu_module.cpp#trig_functions
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
// enable=False re-enables whichever sensors are actually present.
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

// Flat module, no type/instance (android.imu.acceleration_mg(), not
// android.imu.IMU().acceleration_mg()). Matches upstream py_imu.c's
// own shape exactly (its globals_dict_table holds the function objects
// directly, no make_new/no mp_obj_type_t at all).
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

// see session-state: imu_module.cpp#android_proximity_distance_cm
// see session-state: imu_module.cpp#extern_linkage
mp_obj_t android_proximity_distance_cm() {
    ensure_proximity_enabled();
    if (!g_imu.proximity) {
        raise_os_error(MP_ENODEV, "android.proximity: no proximity sensor on this device");
    }
    if (!g_imu.proximity_has_reading) {
        // First read since enabling. On-change sensors are documented
        // to deliver one event immediately on enable, reporting the
        // CURRENT state, so blocking here is the same "genuine
        // malfunction if it times out" case as accel/gyro's first read.
        ASensorEvent event;
        if (!wait_for_fresh_event(ASENSOR_TYPE_PROXIMITY, &event)) {
            raise_os_error(MP_ETIMEDOUT, "android.proximity: read timed out");
        }
        g_imu.proximity_last_distance = event.distance;
        g_imu.proximity_has_reading = true;
    } else {
        // Steady state: a single non-blocking poll, cached value returned
        // if nothing changed. see session-state:
        // imu_module.cpp#android_proximity_distance_cm
        ALooper_pollOnce(0, nullptr, nullptr, nullptr);
        ASensorEvent event;
        while (ASensorEventQueue_getEvents(g_imu.queue, &event, 1) > 0) {
            if (event.type == ASENSOR_TYPE_PROXIMITY) {
                g_imu.proximity_last_distance = event.distance;
            }
        }
    }
    return mp_obj_new_float(g_imu.proximity_last_distance);
}
extern MP_DEFINE_CONST_FUN_OBJ_0(android_proximity_distance_cm_obj, android_proximity_distance_cm);

// see session-state: imu_module.cpp#extern_linkage
extern "C" const mp_obj_module_t imu_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &imu_module_globals,
};

extern "C" void imu_close_all(void) {
    teardown_queue();
    // manager/accel/gyro/looper intentionally left set.
    // see session-state: imu_module.cpp#ImuState
}
