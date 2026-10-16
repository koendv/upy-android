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
    if (strstr(err, "http: bad argument") != nullptr) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s", err);
        free(err);
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s"), msg);
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

// headers={name: value, ...} and auth=(user, password), read once per call.
// The strings belong to the objects the caller passed, so they stay valid.
struct Options {
    const char **headers = nullptr;
    size_t header_count = 0;
    const char *user = nullptr;
    const char *password = nullptr;

    http_options_t bridge() const {
        return {headers, header_count, user, password};
    }
    ~Options() {
        free(headers);
    }
};

// Header names and values are plain ASCII, without control characters; a name
// is not empty and has no space or colon. Anything else is a ValueError.
void check_header_text(const char *text, bool is_name) {
    if (is_name && *text == '\0') {
        mp_raise_ValueError(MP_ERROR_TEXT("http: bad argument: empty header name"));
    }
    for (const unsigned char *c = (const unsigned char *) text; *c; c++) {
        bool control = (*c < 0x20 && *c != '\t') || *c >= 0x7f;
        if (control || (is_name && (*c == ' ' || *c == '\t' || *c == ':'))) {
            mp_raise_ValueError(MP_ERROR_TEXT("http: bad argument: header text must be plain ASCII"));
        }
    }
}

void parse_options(mp_obj_t headers_obj, mp_obj_t auth_obj, Options *options) {
    // First: a raise after the headers are copied would leave the copy behind.
    if (auth_obj != mp_const_none) {
        size_t len;
        mp_obj_t *items;
        if (!(mp_obj_is_type(auth_obj, &mp_type_tuple) || mp_obj_is_type(auth_obj, &mp_type_list))) {
            mp_raise_TypeError(MP_ERROR_TEXT("http: auth must be (user, password)"));
        }
        mp_obj_get_array(auth_obj, &len, &items);
        if (len != 2 || !mp_obj_is_str(items[0]) || !mp_obj_is_str(items[1])) {
            mp_raise_TypeError(MP_ERROR_TEXT("http: auth must be (user, password)"));
        }
        options->user = mp_obj_str_get_str(items[0]);
        options->password = mp_obj_str_get_str(items[1]);
    }

    if (headers_obj != mp_const_none) {
        if (!mp_obj_is_dict_or_ordereddict(headers_obj)) {
            mp_raise_TypeError(MP_ERROR_TEXT("http: headers must be a dict"));
        }
        mp_map_t *map = mp_obj_dict_get_map(headers_obj);
        // No raise below may leave the allocation behind, so it is checked first.
        size_t count = 0;
        for (size_t i = 0; i < map->alloc; i++) {
            if (!mp_map_slot_is_filled(map, i)) {
                continue;
            }
            if (!mp_obj_is_str(map->table[i].key) || !mp_obj_is_str(map->table[i].value)) {
                mp_raise_TypeError(MP_ERROR_TEXT("http: header names and values must be str"));
            }
            check_header_text(mp_obj_str_get_str(map->table[i].key), true);
            check_header_text(mp_obj_str_get_str(map->table[i].value), false);
            count++;
        }
        options->headers = (const char **) malloc(sizeof(char *) * 2 * (count > 0 ? count : 1));
        size_t n = 0;
        for (size_t i = 0; i < map->alloc; i++) {
            if (mp_map_slot_is_filled(map, i)) {
                options->headers[n++] = mp_obj_str_get_str(map->table[i].key);
                options->headers[n++] = mp_obj_str_get_str(map->table[i].value);
            }
        }
        options->header_count = count;
    }
}

// http.get(url, timeout=30, headers=None, auth=None) -> bytes
mp_obj_t http_get(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_url, ARG_timeout, ARG_headers, ARG_auth };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_url, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_timeout, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_headers, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        {MP_QSTR_auth, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *url = mp_obj_str_get_str(args[ARG_url].u_obj);
    double timeout = parse_timeout(args[ARG_timeout].u_obj);
    // Before the options are copied: a raise here leaves nothing to free.
    begin_call();
    Options options;
    parse_options(args[ARG_headers].u_obj, args[ARG_auth].u_obj, &options);
    http_options_t bridge_options = options.bridge();
    uint8_t *data = nullptr;
    size_t len = 0;
    char *err = nullptr;
    bool ok = http_bridge_get(url, timeout, max_body_bytes(), &bridge_options, &data, &len, &err);
    free(options.headers);
    options.headers = nullptr;
    if (!ok) {
        raise_http_error_free(err);
    }
    return body_to_bytes(data, len);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(http_get_obj, 0, http_get);

// http.post(url, data, timeout=30, headers=None, auth=None) -> bytes, and put() the same.
mp_obj_t send_request(const char *method, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_url, ARG_data, ARG_timeout, ARG_headers, ARG_auth };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_url, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_data, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_timeout, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_headers, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        {MP_QSTR_auth, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
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
    // Before the options are copied: a raise here leaves nothing to free.
    begin_call();
    Options options;
    parse_options(args[ARG_headers].u_obj, args[ARG_auth].u_obj, &options);
    http_options_t bridge_options = options.bridge();

    uint8_t *data = nullptr;
    size_t len = 0;
    char *err = nullptr;
    bool ok = http_bridge_send(method, url, payload, payload_len, content_type, timeout, max_body_bytes(),
                               &bridge_options, &data, &len, &err);
    free(options.headers);
    options.headers = nullptr;
    if (!ok) {
        raise_http_error_free(err);
    }
    return body_to_bytes(data, len);
}

mp_obj_t http_post(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    return send_request("POST", n_args, pos_args, kw_args);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(http_post_obj, 0, http_post);

mp_obj_t http_put(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    return send_request("PUT", n_args, pos_args, kw_args);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(http_put_obj, 0, http_put);

// http.download(url, path, progress=False, timeout=30, headers=None, auth=None) -> None
mp_obj_t http_download(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_url, ARG_path, ARG_progress, ARG_timeout, ARG_headers, ARG_auth };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_url, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_path, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_progress, MP_ARG_BOOL, {.u_bool = false}},
        {MP_QSTR_timeout, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_headers, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        {MP_QSTR_auth, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *url = mp_obj_str_get_str(args[ARG_url].u_obj);
    const char *path = mp_obj_str_get_str(args[ARG_path].u_obj);
    double timeout = parse_timeout(args[ARG_timeout].u_obj);
    // Before the options are copied: a raise here leaves nothing to free.
    begin_call();
    Options options;
    parse_options(args[ARG_headers].u_obj, args[ARG_auth].u_obj, &options);
    http_options_t bridge_options = options.bridge();
    char *err = nullptr;
    bool ok = http_bridge_download(url, path, args[ARG_progress].u_bool, timeout, &bridge_options, &err);
    free(options.headers);
    options.headers = nullptr;
    if (!ok) {
        raise_http_error_free(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(http_download_obj, 0, http_download);

const char http_help_text[] =
    "http: web requests over okhttp. Every call waits for the result. Not the requests API.\n"
    "  get(url, timeout=30, headers=None, auth=None)               -> bytes, the response body\n"
    "  post(url, data, timeout=30, headers=None, auth=None)        -> bytes; data is str (UTF-8, text/plain) or bytes\n"
    "  put(url, data, timeout=30, headers=None, auth=None)         -> bytes; like post, for WebDAV and similar\n"
    "  download(url, path, progress=False, timeout=30, headers=None, auth=None) -> None; writes the response body to path\n"
    "headers: a dict of str to str, e.g. {'Authorization': 'Bearer ' + token}. A 'Content-Type' header replaces\n"
    "  the default of post and put (application/octet-stream for bytes, text/plain for str). Plain ASCII only,\n"
    "  else ValueError.\n"
    "auth: (user, password), HTTP Basic, sent with the first request. Over http:// it is sent in the clear: use https.\n"
    "  An Authorization header is not sent to another host after a redirect. A 'Authorization' in headers replaces auth.\n"
    "timeout: seconds, int or float, for connect, read and write. The read timeout is per read, not\n"
    "  for the whole transfer. 0: no timeout.\n"
    "redirects are followed, also from http to https. https uses the system's trusted certificates.\n"
    "  Plain http:// is allowed, for devices on the local network. A 301 or 302 answer to a post or put is\n"
    "  followed as a get, without the data: use the final https:// URL for uploads.\n"
    "get, post and put: the whole body must fit in free memory, else MemoryError; use download for large files.\n"
    "download: path is relative to the files folder, like open(). The body goes to path.part and is\n"
    "  renamed when complete, so a failed download leaves no partial file. progress=True prints\n"
    "  percentages (bytes, when the size is not known).\n"
    "example, a jpeg to WebDAV: http.put('https://host/dav/pic.jpg', jpeg, headers={'Content-Type': 'image/jpeg'},\n"
    "  auth=('user', 'password'))\n"
    "example, multipart: boundary = 'xyz'; body = b'--xyz\\r\\nContent-Disposition: form-data; name=\"f\"; filename=\"a.jpg\"\\r\\n'\n"
    "  b'Content-Type: image/jpeg\\r\\n\\r\\n' + jpeg + b'\\r\\n--xyz--\\r\\n'; headers={'Content-Type': 'multipart/form-data; boundary=xyz'}\n"
    "errors: OSError. HTTP status 400 or higher: 'HTTP 404' (401 and 403 for a refused login). ETIMEDOUT on a timeout,\n"
    "  otherwise EIO with the reason. A bad URL, header or content type is a ValueError.\n"
    "  Interrupt ends the call with KeyboardInterrupt.\n"
    "limits: no cookies, no response headers or status, no other methods\n"
;
mp_obj_t http_help() {
    return mp_obj_new_str(http_help_text, strlen(http_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(http_help_obj, http_help);

const mp_rom_map_elem_t http_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_http)},
    {MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&http_get_obj)},
    {MP_ROM_QSTR(MP_QSTR_post), MP_ROM_PTR(&http_post_obj)},
    {MP_ROM_QSTR(MP_QSTR_put), MP_ROM_PTR(&http_put_obj)},
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
