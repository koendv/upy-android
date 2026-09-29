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

int location_bridge_start(long long interval_ms, float min_distance_m);

void location_bridge_stop(void);

// Fills fix[8]: latitude, longitude, altitude_m, accuracy_m, speed_mps,
// bearing_deg, time_ms (NaN where Android has no value), seq (0 for
// last_known) and provider
// (up to provider_len bytes, NUL-terminated). Returns false without a fix.
bool location_bridge_read(bool last_known, double *fix, char *provider, int provider_len);

// Sequence number of the newest fix Android delivered; 0 before any.
long long location_bridge_seq(void);

// Blocks up to wait_ms, or until a new fix or location_interrupt_wait().
void location_bridge_wait(long wait_ms);

#ifdef __cplusplus
}
#endif

#endif
