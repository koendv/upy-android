// upy-android native umqtt module. OUR OWN code, NOT vendored OpenMV
// source.
#ifndef UPY_ANDROID_MQTT_MODULE_H
#define UPY_ANDROID_MQTT_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

// Called once, from engine_jni.cpp's nativeInit(), after g_jvm is
// cached -- resolves and caches MqttShim's class/method IDs. jni_env is
// a JNIEnv* -- typed void* here so this header (included by
// engine_jni.cpp, which does have <jni.h>, but kept symmetric with
// litert_module.h/mediastore_module.h's own no-real-JNI-types
// convention) stays consistent project-wide.
void mqtt_bridge_init(void *jni_env);

// Idempotent, safe to call when nothing is open. Same tier/contract as
// camera_close_all()/tf_close_all()/rt_close_all()/litert_close_all(),
// called from the same two places in engine_jni.cpp, BEFORE
// mp_embed_deinit(). Disconnects every still-open MQTTClient.
void mqtt_close_all(void);

#ifdef __cplusplus
}
#endif

#endif
