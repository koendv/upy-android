// upy-android native umqtt module (top-level `umqtt`, matching a real/
// expected module name -- same tier as `litert`/`ulab`, not
// Android-specific glue, see Part 7 of the dev-workflow-speedups plan).
// OUR OWN code, NOT vendored OpenMV/micropython-lib source.
//
// Matches real umqtt.simple's own script-facing API shape:
// MQTTClient(client_id, server, port=0, user=None, password=None,
// keepalive=0), set_callback(f), connect(clean_session=True),
// disconnect(), ping(), publish(topic, msg, retain=False, qos=0),
// subscribe(topic, qos=0), check_msg(), wait_msg() -- backed by HiveMQ
// MQTT Client (com.hivemq:hivemq-mqtt-client) via MqttShim.kt/
// mqtt_jni_bridge.cpp, not a from-scratch MQTT wire-protocol
// implementation. Also exposed as umqtt.simple.MQTTClient (the SAME
// type object, not a copy) for real drop-in compatibility with scripts
// written against micropython-lib's `from umqtt.simple import
// MQTTClient` -- umqtt.robust is deliberately NOT aliased the same way:
// robust.MQTTClient's whole point is transparent auto-reconnect/retry
// logic on top of simple's API, which this module does not implement,
// and aliasing it would silently overclaim that behavior.
//
// KNOWN, DELIBERATE GAPS vs. real umqtt.simple (documented, not hidden):
// - ssl=True raises NotImplementedError rather than silently connecting
//   in plaintext -- TLS wiring is real future work, not done here.
// - ping() checks HiveMQ's own client connection state rather than
//   sending a real PINGREQ and waiting for PINGRESP -- HiveMQ's
//   Mqtt3BlockingClient has no public ping() of its own; the client
//   already sends real keep-alive PINGREQs internally in the
//   background for as long as the connection is open, so this is a
//   liveness check, not a wire-level round trip.
// - set_last_will (real umqtt.simple) is not implemented.
//
// This file is qstr-scanned (SRC_QSTR in micropython_embed.mk).
// #include <jni.h> is banned (no qstr-stub for it) -- all JNI calls
// live in mqtt_jni_bridge.cpp, reached only through mqtt_jni_bridge.h's
// primitive-typed (void*) boundary.

#include <cstdlib>
#include <cstring>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"
}

#include "mqtt_jni_bridge.h"
#include "mqtt_module.h"

namespace {

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// msg is a malloc'd (strdup'd) string from mqtt_jni_bridge.cpp,
// describing a real Kotlin/JNI exception -- copied into a new
// MicroPython str (which copies internally) before being freed here.
void raise_os_error_free(int errno_, char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    free(msg);
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// Native-only registry, same reasoning as litert_module.cpp's own
// LitertHandleNode/registry_design -- deliberately never holds an
// mp_obj_t pointer, so mqtt_close_all() (called from engine_jni.cpp
// BEFORE mp_embed_deinit(), when no Python object graph can be walked
// safely any more) can still disconnect every open connection.
struct MqttHandleNode {
    void *global_ref;
    MqttHandleNode *next;
};

MqttHandleNode *g_mqtt_connections = nullptr;

MqttHandleNode *registry_add(void *global_ref) {
    auto *node = (MqttHandleNode *) malloc(sizeof(MqttHandleNode));
    node->global_ref = global_ref;
    node->next = g_mqtt_connections;
    g_mqtt_connections = node;
    return node;
}

void registry_remove(MqttHandleNode *node) {
    if (!node) {
        return;
    }
    MqttHandleNode **link = &g_mqtt_connections;
    while (*link) {
        if (*link == node) {
            *link = node->next;
            free(node);
            return;
        }
        link = &(*link)->next;
    }
}

// user/password/keepalive are captured at construction time and reused
// at connect() time, matching real umqtt.simple's own constructor
// (which stashes them as self.user/self.pswd/self.keepalive for the
// same reason). Storing them as plain mp_obj_t fields on a
// mp_obj_malloc'd struct is safe: MicroPython's GC is a conservative
// mark-sweep over the whole heap, not schema-driven, so it traces any
// pointer-shaped field regardless of the struct's own layout -- same
// reasoning already relied on for the `callback` field below.
struct mqtt_client_obj_t {
    mp_obj_base_t base;
    MqttHandleNode *node;  // null once closed
    mp_obj_t callback;     // set_callback(f)'s f, or mp_const_none
    mp_obj_t user_obj;     // str or none
    mp_obj_t password_obj; // str/bytes or none
    mp_int_t keepalive;
};

void raise_if_closed(mqtt_client_obj_t *self) {
    if (!self->node) {
        raise_os_error(MP_EINVAL, "umqtt: MQTTClient closed -- construct a new one");
    }
}

mp_obj_t mqtt_client_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_client_id, ARG_server, ARG_port, ARG_user, ARG_password, ARG_keepalive, ARG_ssl, ARG_ssl_params };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_client_id, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_server, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_port, MP_ARG_INT, {.u_int = 0}},
        {MP_QSTR_user, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_password, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_keepalive, MP_ARG_INT, {.u_int = 0}},
        {MP_QSTR_ssl, MP_ARG_BOOL, {.u_bool = false}},
        {MP_QSTR_ssl_params, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    if (parsed[ARG_ssl].u_bool) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("umqtt: ssl=True not implemented yet"));
    }

    const char *client_id = mp_obj_str_get_str(parsed[ARG_client_id].u_obj);
    const char *server = mp_obj_str_get_str(parsed[ARG_server].u_obj);
    mp_int_t port = parsed[ARG_port].u_int != 0 ? parsed[ARG_port].u_int : 1883;

    void *global_ref = nullptr;
    char *err = nullptr;
    if (!mqtt_bridge_create(client_id, server, (int) port, &global_ref, &err)) {
        raise_os_error_free(MP_EINVAL, err);
    }

    auto *self = mp_obj_malloc_with_finaliser(mqtt_client_obj_t, type);
    self->node = registry_add(global_ref);
    self->callback = mp_const_none;
    self->user_obj = parsed[ARG_user].u_obj != MP_OBJ_NULL ? parsed[ARG_user].u_obj : mp_const_none;
    self->password_obj = parsed[ARG_password].u_obj != MP_OBJ_NULL ? parsed[ARG_password].u_obj : mp_const_none;
    self->keepalive = parsed[ARG_keepalive].u_int;
    return MP_OBJ_FROM_PTR(self);
}

mp_obj_t mqtt_client_set_callback(mp_obj_t self_in, mp_obj_t f_in) {
    auto *self = (mqtt_client_obj_t *) MP_OBJ_TO_PTR(self_in);
    self->callback = f_in;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mqtt_client_set_callback_obj, mqtt_client_set_callback);

mp_obj_t mqtt_client_connect(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_clean_session, MP_ARG_BOOL, {.u_bool = true}},
    };
    auto *self = (mqtt_client_obj_t *) MP_OBJ_TO_PTR(pos_args[0]);
    raise_if_closed(self);

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    const char *username = self->user_obj != mp_const_none ? mp_obj_str_get_str(self->user_obj) : nullptr;
    const uint8_t *password = nullptr;
    size_t password_len = 0;
    mp_buffer_info_t password_bufinfo;
    if (self->password_obj != mp_const_none) {
        mp_get_buffer_raise(self->password_obj, &password_bufinfo, MP_BUFFER_READ);
        password = (const uint8_t *) password_bufinfo.buf;
        password_len = password_bufinfo.len;
    }

    bool session_present = false;
    char *err = nullptr;
    if (!mqtt_bridge_connect(self->node->global_ref, username, password, password_len,
                              parsed[0].u_bool, (int) self->keepalive, &session_present, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    return mp_obj_new_int(session_present ? 1 : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mqtt_client_connect_obj, 1, mqtt_client_connect);

void mqtt_client_disconnect_impl(mqtt_client_obj_t *self) {
    if (!self->node) {
        return;
    }
    char *err = nullptr;
    if (!mqtt_bridge_disconnect(self->node->global_ref, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
}

mp_obj_t mqtt_client_disconnect(mp_obj_t self_in) {
    mqtt_client_disconnect_impl((mqtt_client_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mqtt_client_disconnect_obj, mqtt_client_disconnect);

mp_obj_t mqtt_client_ping(mp_obj_t self_in) {
    auto *self = (mqtt_client_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);

    bool connected = false;
    char *err = nullptr;
    if (!mqtt_bridge_is_connected(self->node->global_ref, &connected, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    if (!connected) {
        raise_os_error(MP_ENOTCONN, "umqtt: not connected");
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mqtt_client_ping_obj, mqtt_client_ping);

mp_obj_t mqtt_client_publish(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_topic, ARG_msg, ARG_retain, ARG_qos };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_topic, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_msg, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_retain, MP_ARG_BOOL, {.u_bool = false}},
        {MP_QSTR_qos, MP_ARG_INT, {.u_int = 0}},
    };
    auto *self = (mqtt_client_obj_t *) MP_OBJ_TO_PTR(pos_args[0]);
    raise_if_closed(self);

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    const char *topic = mp_obj_str_get_str(parsed[ARG_topic].u_obj);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(parsed[ARG_msg].u_obj, &bufinfo, MP_BUFFER_READ);

    char *err = nullptr;
    if (!mqtt_bridge_publish(self->node->global_ref, topic, (const uint8_t *) bufinfo.buf, bufinfo.len,
                              (int) parsed[ARG_qos].u_int, parsed[ARG_retain].u_bool, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mqtt_client_publish_obj, 3, mqtt_client_publish);

mp_obj_t mqtt_client_subscribe(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_topic, ARG_qos };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_topic, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_qos, MP_ARG_INT, {.u_int = 0}},
    };
    auto *self = (mqtt_client_obj_t *) MP_OBJ_TO_PTR(pos_args[0]);
    raise_if_closed(self);

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    const char *topic = mp_obj_str_get_str(parsed[ARG_topic].u_obj);
    char *err = nullptr;
    if (!mqtt_bridge_subscribe(self->node->global_ref, topic, (int) parsed[ARG_qos].u_int, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mqtt_client_subscribe_obj, 2, mqtt_client_subscribe);

// Shared by check_msg()/wait_msg() -- polls once with the given
// timeout, and if a message was queued, invokes self->callback(topic,
// msg) (real umqtt.simple contract: the callback receives bytes for
// both topic and msg, not str -- matched here via mp_obj_new_bytes for
// both, not mp_obj_new_str).
bool poll_and_dispatch(mqtt_client_obj_t *self, long timeout_ms) {
    bool has_message = false;
    char *topic = nullptr;
    uint8_t *payload = nullptr;
    size_t payload_len = 0;
    char *err = nullptr;
    if (!mqtt_bridge_poll(self->node->global_ref, timeout_ms, &has_message,
                          &topic, &payload, &payload_len, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    if (!has_message) {
        return false;
    }
    if (self->callback != mp_const_none) {
        mp_obj_t topic_obj = mp_obj_new_bytes((const byte *) topic, strlen(topic));
        mp_obj_t payload_obj = mp_obj_new_bytes((const byte *) payload, payload_len);
        mp_call_function_2(self->callback, topic_obj, payload_obj);
    }
    free(topic);
    free(payload);
    return true;
}

mp_obj_t mqtt_client_check_msg(mp_obj_t self_in) {
    auto *self = (mqtt_client_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);
    poll_and_dispatch(self, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mqtt_client_check_msg_obj, mqtt_client_check_msg);

// Chunked-wait pattern, same reasoning/idiom as camera_module.cpp's own
// wait_and_acquire_frame(): a single truly-indefinite blocking JNI call
// would never observe a pending interrupt (Ctrl-C/interrupt()), since
// nothing polls mp_handle_pending() while blocked inside Kotlin/Java
// code. Real umqtt.simple's own wait_msg() blocks the whole interpreter
// uninterruptibly (a raw blocking socket recv()) -- this is a real,
// deliberate improvement over that upstream limitation, not a
// deviation script authors need to work around.
constexpr long kWaitChunkMs = 250;

mp_obj_t mqtt_client_wait_msg(mp_obj_t self_in) {
    auto *self = (mqtt_client_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);
    for (;;) {
        if (poll_and_dispatch(self, kWaitChunkMs)) {
            return mp_const_none;
        }
        // Raises (nlr jump) if the user tapped Interrupt while waiting.
        mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_1(mqtt_client_wait_msg_obj, mqtt_client_wait_msg);

void mqtt_client_close_impl(mqtt_client_obj_t *self) {
    if (!self->node) {
        return;
    }
    MqttHandleNode *node = self->node;
    self->node = nullptr;
    char *err = nullptr;
    if (!mqtt_bridge_disconnect(node->global_ref, &err)) {
        free(err);  // best-effort on close/__del__, same as litert_close_all()'s own teardown
    }
    mqtt_bridge_delete_global_ref(node->global_ref);
    registry_remove(node);
}

mp_obj_t mqtt_client_close(mp_obj_t self_in) {
    mqtt_client_close_impl((mqtt_client_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mqtt_client_close_obj, mqtt_client_close);

mp_obj_t mqtt_client_del(mp_obj_t self_in) {
    mqtt_client_close_impl((mqtt_client_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mqtt_client_del_obj, mqtt_client_del);

const mp_rom_map_elem_t mqtt_client_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_set_callback), MP_ROM_PTR(&mqtt_client_set_callback_obj)},
    {MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&mqtt_client_connect_obj)},
    {MP_ROM_QSTR(MP_QSTR_disconnect), MP_ROM_PTR(&mqtt_client_disconnect_obj)},
    {MP_ROM_QSTR(MP_QSTR_ping), MP_ROM_PTR(&mqtt_client_ping_obj)},
    {MP_ROM_QSTR(MP_QSTR_publish), MP_ROM_PTR(&mqtt_client_publish_obj)},
    {MP_ROM_QSTR(MP_QSTR_subscribe), MP_ROM_PTR(&mqtt_client_subscribe_obj)},
    {MP_ROM_QSTR(MP_QSTR_check_msg), MP_ROM_PTR(&mqtt_client_check_msg_obj)},
    {MP_ROM_QSTR(MP_QSTR_wait_msg), MP_ROM_PTR(&mqtt_client_wait_msg_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mqtt_client_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mqtt_client_del_obj)},
};
MP_DEFINE_CONST_DICT(mqtt_client_locals_dict, mqtt_client_locals_dict_table);

}  // namespace

extern MP_DEFINE_CONST_OBJ_TYPE(
    mqtt_client_type,
    MP_QSTR_MQTTClient,
    MP_TYPE_FLAG_NONE,
    make_new, mqtt_client_make_new,
    locals_dict, &mqtt_client_locals_dict
    );

namespace {

// umqtt.simple submodule -- SAME MQTTClient type object as the
// top-level one, not a copy, so `isinstance()`/type identity checks
// behave exactly as if only one module existed. Real drop-in
// compatibility target: `from umqtt.simple import MQTTClient`.
const mp_rom_map_elem_t umqtt_simple_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_simple)},
    {MP_ROM_QSTR(MP_QSTR_MQTTClient), MP_ROM_PTR(&mqtt_client_type)},
};
MP_DEFINE_CONST_DICT(umqtt_simple_globals, umqtt_simple_globals_table);

const mp_obj_module_t umqtt_simple_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &umqtt_simple_globals,
};

const mp_rom_map_elem_t umqtt_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_umqtt)},
    {MP_ROM_QSTR(MP_QSTR_MQTTClient), MP_ROM_PTR(&mqtt_client_type)},
    {MP_ROM_QSTR(MP_QSTR_simple), MP_ROM_PTR(&umqtt_simple_module)},
};
MP_DEFINE_CONST_DICT(umqtt_module_globals, umqtt_module_globals_table);

}  // namespace

// Top-level module -- matches a real/expected module name (umqtt.simple/
// umqtt.robust from micropython-lib), same tier as litert/ulab, not
// Android-specific glue. See this file's own header comment.
extern "C" const mp_obj_module_t umqtt_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &umqtt_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_umqtt, umqtt_module);

extern "C" void mqtt_bridge_init(void *jni_env) {
    mqtt_bridge_init_impl(jni_env);
}

extern "C" void mqtt_close_all(void) {
    while (g_mqtt_connections) {
        MqttHandleNode *node = g_mqtt_connections;
        char *err = nullptr;
        mqtt_bridge_disconnect(node->global_ref, &err);
        free(err);
        mqtt_bridge_delete_global_ref(node->global_ref);
        g_mqtt_connections = node->next;
        free(node);
    }
}
