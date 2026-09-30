// upy-android native umqtt module.
// see session-state: mqtt_module.cpp#mqtt_client_make_new
//
// API follows micropython-lib umqtt.simple 1.8.1: ssl=True uses TLS
// with Android's CA certificates; ssl_params other than server_hostname,
// and an SSLContext for ssl, are not supported.
// TODO: revisit mqtt when the MicroPython Android port has socket +
// ssl: micropython-lib's own umqtt.simple would then run unmodified.

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
// describing a real Kotlin/JNI exception. Copied into a new
// MicroPython str (which copies internally) before being freed here.
void raise_os_error_free(int errno_, char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    free(msg);
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// see session-state: mqtt_module.cpp#registry_add
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

// see session-state: mqtt_module.cpp#mqtt_client_obj_t
struct mqtt_client_obj_t {
    mp_obj_base_t base;
    MqttHandleNode *node;  // null once closed
    mp_obj_t callback;     // set_callback(f)'s f, or mp_const_none
    mp_obj_t user_obj;     // str or none
    mp_obj_t password_obj; // str/bytes or none
    mp_int_t keepalive;
    mp_obj_t lw_topic;     // set_last_will(): str or none
    mp_obj_t lw_msg;       // str/bytes
    mp_int_t lw_qos;
    bool lw_retain;
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
        {MP_QSTR_ssl, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        {MP_QSTR_ssl_params, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    // ssl: None/False, or True. An SSLContext cannot occur: this port has
    // no ssl module.
    mp_obj_t ssl_obj = parsed[ARG_ssl].u_obj;
    if (ssl_obj != mp_const_none && ssl_obj != mp_const_false && ssl_obj != mp_const_true) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("umqtt: ssl must be True or False"));
    }
    bool ssl = ssl_obj == mp_const_true;
    // ssl_params: only server_hostname, which TLS checks against server anyway.
    mp_obj_t ssl_params = parsed[ARG_ssl_params].u_obj;
    if (ssl_params != MP_OBJ_NULL && mp_obj_is_type(ssl_params, &mp_type_dict)) {
        mp_map_t *map = mp_obj_dict_get_map(ssl_params);
        for (size_t i = 0; i < map->alloc; i++) {
            if (mp_map_slot_is_filled(map, i) &&
                !mp_obj_equal(map->table[i].key, MP_OBJ_NEW_QSTR(MP_QSTR_server_hostname))) {
                mp_raise_NotImplementedError(MP_ERROR_TEXT("umqtt: only server_hostname supported in ssl_params"));
            }
        }
    }

    const char *client_id = mp_obj_str_get_str(parsed[ARG_client_id].u_obj);
    const char *server = mp_obj_str_get_str(parsed[ARG_server].u_obj);
    mp_int_t port = parsed[ARG_port].u_int != 0 ? parsed[ARG_port].u_int : (ssl ? 8883 : 1883);

    void *global_ref = nullptr;
    char *err = nullptr;
    if (!mqtt_bridge_create(client_id, server, (int) port, ssl, &global_ref, &err)) {
        raise_os_error_free(MP_EINVAL, err);
    }

    auto *self = mp_obj_malloc_with_finaliser(mqtt_client_obj_t, type);
    self->node = registry_add(global_ref);
    self->callback = mp_const_none;
    self->user_obj = parsed[ARG_user].u_obj != MP_OBJ_NULL ? parsed[ARG_user].u_obj : mp_const_none;
    self->password_obj = parsed[ARG_password].u_obj != MP_OBJ_NULL ? parsed[ARG_password].u_obj : mp_const_none;
    self->keepalive = parsed[ARG_keepalive].u_int;
    self->lw_topic = mp_const_none;
    self->lw_msg = mp_const_none;
    self->lw_qos = 0;
    self->lw_retain = false;
    return MP_OBJ_FROM_PTR(self);
}

// Sent by the broker if this client disconnects uncleanly; applied at
// the next connect(), as in umqtt.simple.
mp_obj_t mqtt_client_set_last_will(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_topic, ARG_msg, ARG_retain, ARG_qos };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_topic, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_msg, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_retain, MP_ARG_BOOL, {.u_bool = false}},
        {MP_QSTR_qos, MP_ARG_INT, {.u_int = 0}},
    };
    auto *self = (mqtt_client_obj_t *) MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    mp_int_t qos = parsed[ARG_qos].u_int;
    if (qos < 0 || qos > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("umqtt: qos must be 0, 1 or 2"));
    }
    if (!mp_obj_is_true(parsed[ARG_topic].u_obj)) {
        mp_raise_ValueError(MP_ERROR_TEXT("umqtt: empty topic"));
    }
    mp_obj_str_get_str(parsed[ARG_topic].u_obj);  // must be a str
    mp_buffer_info_t check;
    mp_get_buffer_raise(parsed[ARG_msg].u_obj, &check, MP_BUFFER_READ);
    self->lw_topic = parsed[ARG_topic].u_obj;
    self->lw_msg = parsed[ARG_msg].u_obj;
    self->lw_qos = qos;
    self->lw_retain = parsed[ARG_retain].u_bool;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mqtt_client_set_last_will_obj, 3, mqtt_client_set_last_will);

mp_obj_t mqtt_client_set_callback(mp_obj_t self_in, mp_obj_t f_in) {
    auto *self = (mqtt_client_obj_t *) MP_OBJ_TO_PTR(self_in);
    self->callback = f_in;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mqtt_client_set_callback_obj, mqtt_client_set_callback);

mp_obj_t mqtt_client_connect(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_clean_session, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_clean_session, MP_ARG_BOOL, {.u_bool = true}},
        {MP_QSTR_timeout, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
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

    // timeout: seconds (int or float) as in umqtt.simple, None: no limit.
    long timeout_ms = 0;
    if (parsed[ARG_timeout].u_obj != mp_const_none) {
        mp_float_t seconds = mp_obj_get_float(parsed[ARG_timeout].u_obj);
        timeout_ms = seconds > 0 ? (long) (seconds * 1000) : 1;
    }

    const char *will_topic = nullptr;
    mp_buffer_info_t will_bufinfo = {};
    if (self->lw_topic != mp_const_none) {
        will_topic = mp_obj_str_get_str(self->lw_topic);
        mp_get_buffer_raise(self->lw_msg, &will_bufinfo, MP_BUFFER_READ);
    }

    bool session_present = false;
    char *err = nullptr;
    if (!mqtt_bridge_connect(self->node->global_ref, username, password, password_len,
                              parsed[ARG_clean_session].u_bool, (int) self->keepalive,
                              will_topic, (const uint8_t *) will_bufinfo.buf, will_bufinfo.len,
                              (int) self->lw_qos, self->lw_retain, timeout_ms,
                              &session_present, &err)) {
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

mp_obj_t mqtt_client_unsubscribe(mp_obj_t self_in, mp_obj_t topic_in) {
    auto *self = (mqtt_client_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);

    const char *topic = mp_obj_str_get_str(topic_in);
    char *err = nullptr;
    if (!mqtt_bridge_unsubscribe(self->node->global_ref, topic, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mqtt_client_unsubscribe_obj, mqtt_client_unsubscribe);

// Shared by check_msg()/wait_msg(). Polls once with the given
// timeout, and if a message was queued, invokes self->callback(topic,
// msg) (real umqtt.simple contract: the callback receives bytes for
// both topic and msg, not str, matched here via mp_obj_new_bytes for
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

// see session-state: mqtt_module.cpp#mqtt_client_wait_msg
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
    {MP_ROM_QSTR(MP_QSTR_set_last_will), MP_ROM_PTR(&mqtt_client_set_last_will_obj)},
    {MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&mqtt_client_connect_obj)},
    {MP_ROM_QSTR(MP_QSTR_disconnect), MP_ROM_PTR(&mqtt_client_disconnect_obj)},
    {MP_ROM_QSTR(MP_QSTR_ping), MP_ROM_PTR(&mqtt_client_ping_obj)},
    {MP_ROM_QSTR(MP_QSTR_publish), MP_ROM_PTR(&mqtt_client_publish_obj)},
    {MP_ROM_QSTR(MP_QSTR_subscribe), MP_ROM_PTR(&mqtt_client_subscribe_obj)},
    {MP_ROM_QSTR(MP_QSTR_unsubscribe), MP_ROM_PTR(&mqtt_client_unsubscribe_obj)},
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

// umqtt.simple submodule. Same MQTTClient type object as the
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

// Top-level module, matches a real/expected module name
// (umqtt.simple/umqtt.robust from micropython-lib).
// see session-state: mqtt_module.cpp#mqtt_client_make_new
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
