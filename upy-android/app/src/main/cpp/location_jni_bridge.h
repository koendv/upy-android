// upy-android: JNI bridge from location_module.cpp to LocationShim.kt.
#ifndef UPY_ANDROID_LOCATION_JNI_BRIDGE_H
#define UPY_ANDROID_LOCATION_JNI_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// location_bridge_start() results, same values as LocationShim.kt.
#define LOCATION_OK 0
#define LOCATION_NO_PERMISSION 1
#define LOCATION_NO_PROVIDER 2

void location_bridge_init_impl(void *jni_env);

int location_bridge_start(long long interval_ms);

void location_bridge_stop(void);

// Fills fix[7]: latitude, longitude, altitude_m, accuracy_m, speed_mps,
// bearing_deg, time_ms (NaN where Android has no value) and provider
// (up to provider_len bytes, NUL-terminated). Returns false without a fix.
bool location_bridge_read(bool last_known, double *fix, char *provider, int provider_len);

#ifdef __cplusplus
}
#endif

#endif
