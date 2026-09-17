// upy-android native imu module -- OUR OWN code, NOT vendored OpenMV
// source. Lives here in app/src/main/cpp/ alongside camera_module.cpp/
// display_module.cpp, NOT under my-overrides/openmv/ -- never copied by
// apply-overrides.sh, compiled directly by the app's own CMake target.
// See SESSION_STATE.yaml's py_imu.c research/scope-decision entries for
// why this is a from-scratch module against Android's NDK sensor API
// rather than a vendoring job: upstream py_imu.c is raw I2C/SPI
// register-level code for a specific ST chip (LSM6DS3/DSM/DSOX), not
// portable -- Android exposes motion sensors only through
// ASensorManager/ASensorEventQueue, never raw register access.
#ifndef UPY_ANDROID_IMU_MODULE_H
#define UPY_ANDROID_IMU_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Idempotent, safe to call when nothing is open -- most scripts never
// touch the imu, so this runs on every ordinary Reset too, same pattern
// as camera_close_all(). Disables any enabled sensors and destroys the
// event queue; the ASensorManager instance, the two ASensor capability
// pointers, and the prepared ALooper are all left in place (see
// imu_module.cpp's own comment) -- none of them are "open" resources
// the way the event queue and enabled-sensor state are.
void imu_close_all(void);

#ifdef __cplusplus
}
#endif

#endif
