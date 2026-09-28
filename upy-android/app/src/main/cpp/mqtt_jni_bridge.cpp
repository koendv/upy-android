// JNI-facing implementation for umqtt (mqtt_module.cpp). One JNI call
// each, through MqttShim.kt. See mqtt_jni_bridge.h's own header
// comment for why mqtt_module.cpp never sees a real jobject/JNIEnv*.

#include "mqtt_jni_bridge.h"

#include <jni.h>

#include <cstdlib>
#include <cstring>

namespace {

JavaVM *g_jvm = nullptr;

jclass g_shim_class = nullptr;

jmethodID g_mid_create = nullptr;
jmethodID g_mid_connect = nullptr;
jmethodID g_mid_disconnect = nullptr;
jmethodID g_mid_publish = nullptr;
jmethodID g_mid_subscribe = nullptr;
jmethodID g_mid_poll = nullptr;
jmethodID g_mid_is_connected = nullptr;

jclass g_message_class = nullptr;
jfieldID g_message_topic_field = nullptr;
jfieldID g_message_payload_field = nullptr;

// Same per-call JNIEnv* lookup as litert_jni_bridge.cpp's own
// current_env(). The worker thread is JVM-attached for :engine's
// entire lifetime, so GetEnv() alone is enough, no Attach/Detach.
JNIEnv *current_env() {
    JNIEnv *env = nullptr;
    g_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    return env;
}

// Same exception-to-string pattern as litert_jni_bridge.cpp's own
// describe_and_clear_exception().
char *describe_and_clear_exception(JNIEnv *env) {
    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!exc) {
        return strdup("umqtt: unknown error");
    }
    jclass throwable_cls = env->FindClass("java/lang/Throwable");
    jmethodID to_string_mid = env->GetMethodID(throwable_cls, "toString", "()Ljava/lang/String;");
    jstring msg = (jstring) env->CallObjectMethod(exc, to_string_mid);
    const char *chars = env->GetStringUTFChars(msg, nullptr);
    char *out = strdup(chars);
    env->ReleaseStringUTFChars(msg, chars);
    env->DeleteLocalRef(msg);
    env->DeleteLocalRef(throwable_cls);
    env->DeleteLocalRef(exc);
    return out;
}

}  // namespace

extern "C" void mqtt_bridge_init_impl(void *jni_env) {
    JNIEnv *env = (JNIEnv *) jni_env;
    env->GetJavaVM(&g_jvm);

    jclass local_shim = env->FindClass("eu/kdvelectronics/upyandroid/mqtt/MqttShim");
    g_shim_class = (jclass) env->NewGlobalRef(local_shim);
    env->DeleteLocalRef(local_shim);

    jclass local_msg = env->FindClass("eu/kdvelectronics/upyandroid/mqtt/MqttMessage");
    g_message_class = (jclass) env->NewGlobalRef(local_msg);
    env->DeleteLocalRef(local_msg);
    g_message_topic_field = env->GetFieldID(g_message_class, "topic", "Ljava/lang/String;");
    g_message_payload_field = env->GetFieldID(g_message_class, "payload", "[B");

    g_mid_create = env->GetStaticMethodID(g_shim_class, "create",
        "(Ljava/lang/String;Ljava/lang/String;I)Leu/kdvelectronics/upyandroid/mqtt/MqttConnection;");
    g_mid_connect = env->GetStaticMethodID(g_shim_class, "connect",
        "(Leu/kdvelectronics/upyandroid/mqtt/MqttConnection;Ljava/lang/String;[BZI)Z");
    g_mid_disconnect = env->GetStaticMethodID(g_shim_class, "disconnect",
        "(Leu/kdvelectronics/upyandroid/mqtt/MqttConnection;)V");
    g_mid_publish = env->GetStaticMethodID(g_shim_class, "publish",
        "(Leu/kdvelectronics/upyandroid/mqtt/MqttConnection;Ljava/lang/String;[BIZ)V");
    g_mid_subscribe = env->GetStaticMethodID(g_shim_class, "subscribe",
        "(Leu/kdvelectronics/upyandroid/mqtt/MqttConnection;Ljava/lang/String;I)V");
    g_mid_poll = env->GetStaticMethodID(g_shim_class, "poll",
        "(Leu/kdvelectronics/upyandroid/mqtt/MqttConnection;J)"
        "Leu/kdvelectronics/upyandroid/mqtt/MqttMessage;");
    g_mid_is_connected = env->GetStaticMethodID(g_shim_class, "isConnected",
        "(Leu/kdvelectronics/upyandroid/mqtt/MqttConnection;)Z");
}

extern "C" bool mqtt_bridge_create(const char *client_id, const char *host, int port,
                                    void **out_global_ref, char **out_err) {
    JNIEnv *env = current_env();
    jstring jclient_id = env->NewStringUTF(client_id);
    jstring jhost = env->NewStringUTF(host);

    jobject local = env->CallStaticObjectMethod(g_shim_class, g_mid_create, jclient_id, jhost, port);

    env->DeleteLocalRef(jclient_id);
    env->DeleteLocalRef(jhost);

    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    *out_global_ref = env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    return true;
}

extern "C" bool mqtt_bridge_connect(void *global_ref, const char *username,
                                     const uint8_t *password, size_t password_len,
                                     bool clean_session, int keepalive_seconds,
                                     bool *out_session_present, char **out_err) {
    JNIEnv *env = current_env();

    jstring jusername = username ? env->NewStringUTF(username) : nullptr;
    jbyteArray jpassword = nullptr;
    if (password) {
        jpassword = env->NewByteArray((jsize) password_len);
        env->SetByteArrayRegion(jpassword, 0, (jsize) password_len, (const jbyte *) password);
    }

    jboolean session_present = env->CallStaticBooleanMethod(g_shim_class, g_mid_connect,
        (jobject) global_ref, jusername, jpassword, (jboolean) clean_session, keepalive_seconds);

    if (jusername) {
        env->DeleteLocalRef(jusername);
    }
    if (jpassword) {
        env->DeleteLocalRef(jpassword);
    }

    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    *out_session_present = session_present;
    return true;
}

extern "C" bool mqtt_bridge_disconnect(void *global_ref, char **out_err) {
    JNIEnv *env = current_env();
    env->CallStaticVoidMethod(g_shim_class, g_mid_disconnect, (jobject) global_ref);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

extern "C" bool mqtt_bridge_publish(void *global_ref, const char *topic,
                                     const uint8_t *payload, size_t payload_len,
                                     int qos, bool retain, char **out_err) {
    JNIEnv *env = current_env();
    jstring jtopic = env->NewStringUTF(topic);
    jbyteArray jpayload = env->NewByteArray((jsize) payload_len);
    env->SetByteArrayRegion(jpayload, 0, (jsize) payload_len, (const jbyte *) payload);

    env->CallStaticVoidMethod(g_shim_class, g_mid_publish, (jobject) global_ref, jtopic,
                               jpayload, qos, (jboolean) retain);

    env->DeleteLocalRef(jtopic);
    env->DeleteLocalRef(jpayload);

    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

extern "C" bool mqtt_bridge_subscribe(void *global_ref, const char *topic, int qos, char **out_err) {
    JNIEnv *env = current_env();
    jstring jtopic = env->NewStringUTF(topic);

    env->CallStaticVoidMethod(g_shim_class, g_mid_subscribe, (jobject) global_ref, jtopic, qos);

    env->DeleteLocalRef(jtopic);

    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    return true;
}

extern "C" bool mqtt_bridge_poll(void *global_ref, long timeout_ms, bool *out_has_message,
                                  char **out_topic, uint8_t **out_payload, size_t *out_payload_len,
                                  char **out_err) {
    JNIEnv *env = current_env();
    jobject jmsg = env->CallStaticObjectMethod(g_shim_class, g_mid_poll, (jobject) global_ref,
                                                (jlong) timeout_ms);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    if (!jmsg) {
        *out_has_message = false;
        return true;
    }

    auto jtopic = (jstring) env->GetObjectField(jmsg, g_message_topic_field);
    auto jpayload = (jbyteArray) env->GetObjectField(jmsg, g_message_payload_field);

    const char *topic_chars = env->GetStringUTFChars(jtopic, nullptr);
    *out_topic = strdup(topic_chars);
    env->ReleaseStringUTFChars(jtopic, topic_chars);

    jsize len = env->GetArrayLength(jpayload);
    auto *buf = (uint8_t *) malloc(len);
    env->GetByteArrayRegion(jpayload, 0, len, (jbyte *) buf);
    *out_payload = buf;
    *out_payload_len = (size_t) len;

    env->DeleteLocalRef(jtopic);
    env->DeleteLocalRef(jpayload);
    env->DeleteLocalRef(jmsg);

    *out_has_message = true;
    return true;
}

extern "C" bool mqtt_bridge_is_connected(void *global_ref, bool *out_connected, char **out_err) {
    JNIEnv *env = current_env();
    jboolean connected = env->CallStaticBooleanMethod(g_shim_class, g_mid_is_connected, (jobject) global_ref);
    if (env->ExceptionCheck()) {
        *out_err = describe_and_clear_exception(env);
        return false;
    }
    *out_connected = connected;
    return true;
}

extern "C" void mqtt_bridge_delete_global_ref(void *global_ref) {
    current_env()->DeleteGlobalRef((jobject) global_ref);
}
