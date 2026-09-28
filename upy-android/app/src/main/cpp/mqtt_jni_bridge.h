// JNI-facing bridge for umqtt (mqtt_module.cpp). Deliberately not
// included by mqtt_module.cpp with <jni.h> types exposed, same
// qstr-scanning reasoning as litert_jni_bridge.h/mediastore_jni_bridge.h.
// Every function here uses only primitive C types (void* for the JNI
// global ref to the backing Kotlin MqttConnection object) so
// mqtt_module.cpp never needs to see a real jobject/JNIEnv*.
// mqtt_jni_bridge.cpp is the one file that #includes <jni.h> and calls
// into MqttShim.kt.
#ifndef UPY_ANDROID_MQTT_JNI_BRIDGE_H
#define UPY_ANDROID_MQTT_JNI_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called once from mqtt_module.h's own mqtt_bridge_init wrapper, same
// indirection litert_module.h/litert_jni_bridge.h use.
void mqtt_bridge_init_impl(void *jni_env);

// Every function below: returns true on success. On failure, returns
// false and sets *out_err to a malloc'd (strdup'd) error string the
// caller (mqtt_module.cpp) must free(), same contract as
// litert_jni_bridge.h's own functions.

bool mqtt_bridge_create(const char *client_id, const char *host, int port,
                         void **out_global_ref, char **out_err);

// username/password may both be null (anonymous connect, real
// umqtt.simple supports this too); password is only ever applied when
// username is non-null. *out_session_present is set only on success.
bool mqtt_bridge_connect(void *global_ref, const char *username,
                          const uint8_t *password, size_t password_len,
                          bool clean_session, int keepalive_seconds,
                          bool *out_session_present, char **out_err);

bool mqtt_bridge_disconnect(void *global_ref, char **out_err);

bool mqtt_bridge_publish(void *global_ref, const char *topic,
                          const uint8_t *payload, size_t payload_len,
                          int qos, bool retain, char **out_err);

bool mqtt_bridge_subscribe(void *global_ref, const char *topic, int qos, char **out_err);

// timeout_ms < 0 blocks indefinitely (a single HiveMQ receive() call,
// mqtt_module.cpp's own wait_msg() calls this in a chunked loop, never
// with a raw negative value directly from Python, so a pending
// interrupt can still be observed between chunks). timeout_ms == 0
// polls once, right now. On success with a message available,
// *out_has_message is set true and *out_topic/*out_payload (malloc'd,
// owned by the caller) are filled in; on success with no message
// available (a timeout, not an error), *out_has_message is set false
// and the out params are left untouched.
bool mqtt_bridge_poll(void *global_ref, long timeout_ms, bool *out_has_message,
                       char **out_topic, uint8_t **out_payload, size_t *out_payload_len,
                       char **out_err);

bool mqtt_bridge_is_connected(void *global_ref, bool *out_connected, char **out_err);

// Releases the JNI global ref. Does not call disconnect() itself.
// mqtt_module.cpp's own close path always calls mqtt_bridge_disconnect
// first (matching a script's own explicit disconnect(), or
// mqtt_close_all()'s reset/deinit teardown), then this.
void mqtt_bridge_delete_global_ref(void *global_ref);

#ifdef __cplusplus
}
#endif

#endif
