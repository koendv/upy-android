// upy-android native http module: get, post and download over okhttp, see
// http_help_text below. Every call blocks the interpreter thread.

#include <cstdlib>
#include <cstring>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/gc.h"
#include "py/mperrno.h"
}

#include "http_jni_bridge.h"
#include "http_module.h"

namespace {

// err is a malloc'd string from http_jni_bridge.cpp: a Java exception as
// text. An Interrupt ends the call with an IOException ("Canceled"); the
// pending KeyboardInterrupt is raised first, so the script sees that instead.
[[noreturn]] void raise_http_error_free(char *err) {
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
    if (strstr(err, "larger than the free memory") != nullptr) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s", err);
        free(err);
        mp_raise_msg_varg(&mp_type_MemoryError, MP_ERROR_TEXT("%s"), msg);
    }
    int errno_ = strstr(err, "SocketTimeoutException") != nullptr ? MP_ETIMEDOUT : MP_EIO;
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(err, strlen(err)),
    };
    free(err);
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// What get() and post() may return: the free heap, which the body must fit into.
long long max_body_bytes() {
    gc_info_t info;
    gc_info(&info);
    return (long long) info.free;
}

double parse_timeout(mp_obj_t obj) {
    return obj == MP_OBJ_NULL ? 30.0 : (double) mp_obj_get_float(obj);
}

// A pending Interrupt is raised here, before the call starts. One that comes
// later reaches the call through http_interrupt().
void begin_call() {
    http_bridge_begin();
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
}

mp_obj_t body_to_bytes(uint8_t *data, size_t len) {
    mp_obj_t result = mp_obj_new_bytes(data, len);
    free(data);
    return result;
}

// http.get(url, timeout=30) -> bytes
mp_obj_t http_get(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_url, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_url, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_timeout, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *url = mp_obj_str_get_str(args[ARG_url].u_obj);
    double timeout = parse_timeout(args[ARG_timeout].u_obj);
    uint8_t *data = nullptr;
    size_t len = 0;
    char *err = nullptr;
    begin_call();
    if (!http_bridge_get(url, timeout, max_body_bytes(), &data, &len, &err)) {
        raise_http_error_free(err);
    }
    return body_to_bytes(data, len);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(http_get_obj, 0, http_get);

// http.post(url, data, timeout=30) -> bytes
mp_obj_t http_post(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_url, ARG_data, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_url, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_data, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_timeout, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *url = mp_obj_str_get_str(args[ARG_url].u_obj);
    double timeout = parse_timeout(args[ARG_timeout].u_obj);
    mp_obj_t data_obj = args[ARG_data].u_obj;
    const uint8_t *payload;
    size_t payload_len;
    const char *content_type;
    mp_buffer_info_t buffer;
    if (mp_obj_is_str(data_obj)) {
        size_t str_len;
        payload = (const uint8_t *) mp_obj_str_get_data(data_obj, &str_len);
        payload_len = str_len;
        content_type = "text/plain; charset=utf-8";
    } else {
        mp_get_buffer_raise(data_obj, &buffer, MP_BUFFER_READ);
        payload = (const uint8_t *) buffer.buf;
        payload_len = buffer.len;
        content_type = "application/octet-stream";
    }

    uint8_t *data = nullptr;
    size_t len = 0;
    char *err = nullptr;
    begin_call();
    if (!http_bridge_post(url, payload, payload_len, content_type, timeout, max_body_bytes(), &data, &len, &err)) {
        raise_http_error_free(err);
    }
    return body_to_bytes(data, len);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(http_post_obj, 0, http_post);

// http.download(url, path, progress=False, timeout=30) -> None
mp_obj_t http_download(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_url, ARG_path, ARG_progress, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_url, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_path, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_progress, MP_ARG_BOOL, {.u_bool = false}},
        {MP_QSTR_timeout, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *url = mp_obj_str_get_str(args[ARG_url].u_obj);
    const char *path = mp_obj_str_get_str(args[ARG_path].u_obj);
    double timeout = parse_timeout(args[ARG_timeout].u_obj);
    char *err = nullptr;
    begin_call();
    if (!http_bridge_download(url, path, args[ARG_progress].u_bool, timeout, &err)) {
        raise_http_error_free(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(http_download_obj, 0, http_download);

const char http_help_text[] =
    "http: web requests over okhttp. Every call waits for the result. Not the requests API.\n"
    "  get(url, timeout=30)                        -> bytes, the response body\n"
    "  post(url, data, timeout=30)                 -> bytes; data is str (UTF-8, text/plain) or bytes\n"
    "  download(url, path, progress=False, timeout=30) -> None; writes the response body to path\n"
    "timeout: seconds, int or float, for connect, read and write. The read timeout is per read, not\n"
    "  for the whole transfer. 0: no timeout.\n"
    "redirects are followed, also from http to https. https uses the system's trusted certificates.\n"
    "  Plain http:// is allowed, for devices on the local network.\n"
    "get and post: the whole body must fit in free memory, else MemoryError; use download for large files.\n"
    "download: path is relative to the files folder, like open(). The body goes to path.part and is\n"
    "  renamed when complete, so a failed download leaves no partial file. progress=True prints\n"
    "  percentages (bytes, when the size is not known).\n"
    "errors: OSError. HTTP status 400 or higher: 'HTTP 404'. ETIMEDOUT on a timeout, otherwise EIO\n"
    "  with the reason. Interrupt ends the call with KeyboardInterrupt.\n"
    "limits: no headers, no other methods, no cookies\n"
;
mp_obj_t http_help() {
    return mp_obj_new_str(http_help_text, strlen(http_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_help_obj, http_help);

const mp_rom_map_elem_t http_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_http)},
    {MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&http_get_obj)},
    {MP_ROM_QSTR(MP_QSTR_post), MP_ROM_PTR(&http_post_obj)},
    {MP_ROM_QSTR(MP_QSTR_download), MP_ROM_PTR(&http_download_obj)},
    {MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&http_help_obj)},
};
MP_DEFINE_CONST_DICT(http_module_globals, http_module_globals_table);

}  // namespace

extern "C" const mp_obj_module_t http_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &http_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_http, http_module);

extern "C" void http_bridge_init(void *jni_env) {
    http_bridge_init_impl(jni_env);
}

extern "C" void http_interrupt(void) {
    http_bridge_cancel();
}
