// upy-android native imu module. OUR OWN code, NOT vendored OpenMV
// source. A from-scratch module against Android's NDK sensor API, not a
// vendoring job: upstream py_imu.c is raw I2C/SPI register-level code
// for a specific ST chip (LSM6DS3/DSM/DSOX), not portable. Android
// exposes motion sensors only through ASensorManager/ASensorEventQueue,
// never raw register access.
#ifndef UPY_ANDROID_IMU_MODULE_H
#define UPY_ANDROID_IMU_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Idempotent, safe to call when nothing is open. Disables any enabled
// sensors and destroys the event queue; the ASensorManager instance,
// the sensor capability pointers, and the prepared ALooper are all left
// in place (see imu_module.cpp's own comment). None of them are "open"
// resources the way the event queue and enabled-sensor state are.
void imu_close_all(void);

#ifdef __cplusplus
}
#endif

#endif
