// upy-android native litert module. OUR OWN code, NOT vendored OpenMV
// source.
// android.litert matches com.google.ai.edge.litert:litert-api's own
// Kotlin interface with literal method-name parity (light style
// adaptation: Python snake_case, not a redesign) -- Environment,
// CompiledModel, TensorBuffer, Options, Accelerator, each exposed
// separately, not folded into one Model god-object the way android.tf/
// android.rt are. Deliberately additive: android.tf and android.rt stay
// exactly as they are, untouched. Eventually android.litert replaces
// both, but not atomically -- android.tf is the only path to NNAPI
// hardware (the newer C API has zero NNAPI code anywhere, confirmed
// against LiteRT's own source), so android.tf stays alive until
// android.litert is proven on real hardware, deleted only afterward as
// its own separate step.
//
// Two genuinely separate mechanisms, not one -- though v0 only actually
// USES one of them:
// - Setup (Environment/CompiledModel/create_input_buffers/
//   create_output_buffers) always goes through LiteRtShim.kt (two JNI
//   crossings, unavoidable -- that logic only exists as Kotlin
//   bytecode, reusing Google's own tested buffer-type/accelerator-
//   option handling rather than re-deriving it, the way rt_module.cpp
//   had to and got wrong twice along the way).
// - The hot path (write_int8/read_int8/run) is DESIGNED around a
//   GLOBAL runtime-switchable backend toggle (set_backend('c'|'kotlin')):
//   'c' would call libLiteRt.so directly on a raw handle extracted via
//   JNI field access (zero further JNI, same approach rt_module.cpp
//   already uses); 'kotlin' calls through the shim (one JNI hop each
//   time). v0 ONLY implements 'kotlin' -- set_backend('c') raises
//   clearly rather than reaching the (already-written, not-yet-enabled)
//   'c' code in litert_jni_bridge.cpp, because learning a Kotlin-
//   created buffer's real byte size via the C API
//   (LiteRtGetTensorBufferPackedSize) returned inconsistent garbage
//   during v0 development despite the extracted handle itself being
//   confirmed genuine -- a real, unresolved issue needing its own
//   investigation, not a design gap. See session-state.
// - close()/__del__ ALWAYS goes through the shim regardless of which
//   hot-path backend was active -- the underlying native object has
//   exactly one owner (Kotlin's own AutoCloseable), so bypassing it for
//   destroy would desync Kotlin's own `destroyed` sentinel from
//   reality. Only read/write/run are toggle-governed.
//
// This file is qstr-scanned (SRC_QSTR in micropython_embed.mk) and
// therefore must NOT #include <jni.h> (no qstr-stub exists for it,
// same reason engine_jni.cpp is excluded from that list) -- all JNI
// calls live in litert_jni_bridge.cpp, reached only through
// litert_jni_bridge.h's primitive-typed (void*/long) boundary.
// see session-state: litert_module.cpp#module_design

#include <cstdlib>
#include <cstring>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"
}

#include "litert_jni_bridge.h"
#include "litert_module.h"

// Forward declarations needed inside the anonymous namespace below --
// these types are only DEFINED (extern MP_DEFINE_CONST_OBJ_TYPE) after
// the namespace closes, same reasoning as rt_model_type's own forward
// declaration in rt_module.cpp. Declaring them OUTSIDE the namespace
// (not just with `extern` inside it) matters: an `extern` declaration
// written inside an anonymous namespace refers to a distinct,
// internally-linked entity of the same name, not the externally-linked
// one defined later outside it -- a real link failure hit and fixed
// while building this file (ld.lld: undefined symbol
// "(anonymous namespace)::litert_tensor_buffer_type").
extern const mp_obj_type_t litert_environment_type;
extern const mp_obj_type_t litert_tensor_buffer_type;
extern const mp_obj_type_t litert_options_type;

namespace {

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// msg is a malloc'd (strdup'd) string from litert_jni_bridge.cpp,
// describing a real Kotlin/JNI exception -- copied into a new MicroPython
// str (which copies internally) before being freed here.
void raise_os_error_free(int errno_, char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    free(msg);
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// 'c' is part of the design (see this file's own header comment) but
// not yet implemented -- defaults to, and in v0 can only be, kKotlin.
enum class LitertBackend { kC, kKotlin };
LitertBackend g_litert_backend = LitertBackend::kKotlin;

void raise_c_backend_not_implemented() {
    raise_os_error(MP_EINVAL,
        "android.litert: the 'c' backend is not yet implemented in this "
        "build -- see session-state for why; use 'kotlin'");
}

// Native-only registry node, deliberately never holds an mp_obj_t
// pointer, same reasoning as tf_module.cpp#registry_design/
// rt_module.cpp#registry_design. global_ref is a JNI global reference
// to the backing Kotlin object (Environment/CompiledModel/TensorBuffer)
// -- needed for the 'kotlin' backend AND for close(), which always goes
// through the Kotlin object regardless of which hot-path backend was
// active. Three separate lists (not one) so litert_close_all() can
// order teardown correctly: buffers and models first (independently
// script-visible, closed in any order), environments last -- mirrors
// rt_close_all()'s own environment-outlives-everything ordering.
struct LitertHandleNode {
    long raw_handle;
    void *global_ref;
    LitertHandleNode *next;
};

LitertHandleNode *g_litert_buffers = nullptr;
LitertHandleNode *g_litert_models = nullptr;
LitertHandleNode *g_litert_environments = nullptr;

LitertHandleNode *registry_add(LitertHandleNode **head, long raw_handle, void *global_ref) {
    auto *node = (LitertHandleNode *) malloc(sizeof(LitertHandleNode));
    node->raw_handle = raw_handle;
    node->global_ref = global_ref;
    node->next = *head;
    *head = node;
    return node;
}

void registry_remove(LitertHandleNode **head, LitertHandleNode *node) {
    if (!node) {
        return;
    }
    LitertHandleNode **link = head;
    while (*link) {
        if (*link == node) {
            *link = node->next;
            free(node);
            return;
        }
        link = &(*link)->next;
    }
}

struct litert_environment_obj_t {
    mp_obj_base_t base;
    LitertHandleNode *node;  // null once closed
};

struct litert_compiled_model_obj_t {
    mp_obj_base_t base;
    LitertHandleNode *node;
};

struct litert_tensor_buffer_obj_t {
    mp_obj_base_t base;
    LitertHandleNode *node;
    // No cached byte-size field in v0 -- see this file's own header
    // comment. write_int8() passes data straight through and relies on
    // Kotlin's own writeInt8() to validate length (raising its own
    // exception on mismatch, already surfaced as an OSError); read_int8()
    // learns its length from the real bridge call's own out-param.
};

struct litert_options_obj_t {
    mp_obj_base_t base;
    mp_int_t accelerator;  // Accelerator.value: NONE=0, CPU=1, GPU=2, NPU=3
};

// ---------------- Environment ----------------

mp_obj_t litert_environment_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    long raw_handle = 0;
    void *global_ref = nullptr;
    char *err = nullptr;
    if (!litert_bridge_create_environment(&raw_handle, &global_ref, &err)) {
        raise_os_error_free(MP_EINVAL, err);
    }

    auto *self = mp_obj_malloc_with_finaliser(litert_environment_obj_t, type);
    self->node = registry_add(&g_litert_environments, raw_handle, global_ref);
    return MP_OBJ_FROM_PTR(self);
}

void litert_environment_close_impl(litert_environment_obj_t *self) {
    if (!self->node) {
        return;
    }
    char *err = nullptr;
    if (!litert_bridge_close_environment(self->node->global_ref, &err)) {
        registry_remove(&g_litert_environments, self->node);
        self->node = nullptr;
        raise_os_error_free(MP_EIO, err);
    }
    registry_remove(&g_litert_environments, self->node);
    self->node = nullptr;
}

mp_obj_t litert_environment_close(mp_obj_t self_in) {
    litert_environment_close_impl((litert_environment_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_environment_close_obj, litert_environment_close);

mp_obj_t litert_environment_del(mp_obj_t self_in) {
    litert_environment_close_impl((litert_environment_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_environment_del_obj, litert_environment_del);

const mp_rom_map_elem_t litert_environment_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&litert_environment_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&litert_environment_del_obj)},
};
MP_DEFINE_CONST_DICT(litert_environment_locals_dict, litert_environment_locals_dict_table);

// ---------------- Options ----------------

mp_obj_t litert_options_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_accelerator };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_accelerator, MP_ARG_INT, {.u_int = 1 /* Accelerator.CPU */}},
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    auto *self = mp_obj_malloc(litert_options_obj_t, type);
    self->accelerator = parsed[ARG_accelerator].u_int;
    return MP_OBJ_FROM_PTR(self);
}

// ---------------- CompiledModel ----------------

void raise_if_model_closed(litert_compiled_model_obj_t *self) {
    if (!self->node) {
        raise_os_error(MP_EINVAL, "android.litert: CompiledModel closed -- construct a new one");
    }
}

mp_obj_t litert_compiled_model_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_environment, ARG_path, ARG_options };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_environment, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_path, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_options, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    if (!mp_obj_is_type(parsed[ARG_environment].u_obj, &litert_environment_type)) {
        raise_os_error(MP_EINVAL, "android.litert: environment must be an android.litert.Environment");
    }
    auto *env_obj = (litert_environment_obj_t *) MP_OBJ_TO_PTR(parsed[ARG_environment].u_obj);
    if (!env_obj->node) {
        raise_os_error(MP_EINVAL, "android.litert: environment is closed");
    }

    const char *path = mp_obj_str_get_str(parsed[ARG_path].u_obj);
    // CompiledModel.create() -> LiteRtCreateModelFromFile is not
    // VFS-aware, same fopen()-based trap already fixed in
    // tf_model_make_new/rt_model_make_new -- strip the leading '/' so a
    // VFS-absolute path resolves correctly.
    // see session-state: tf_module.cpp#tf_model_make_new
    if (path[0] == '/') {
        path++;
    }

    mp_int_t accelerator_value = 1;  // Accelerator.CPU
    if (parsed[ARG_options].u_obj != MP_OBJ_NULL) {
        if (!mp_obj_is_type(parsed[ARG_options].u_obj, &litert_options_type)) {
            raise_os_error(MP_EINVAL, "android.litert: options must be an android.litert.Options");
        }
        accelerator_value = ((litert_options_obj_t *) MP_OBJ_TO_PTR(parsed[ARG_options].u_obj))->accelerator;
    }

    long raw_handle = 0;
    void *global_ref = nullptr;
    char *err = nullptr;
    if (!litert_bridge_create_compiled_model(env_obj->node->global_ref, path,
                                              (int) accelerator_value, &raw_handle,
                                              &global_ref, &err)) {
        raise_os_error_free(MP_EINVAL, err);
    }

    auto *self = mp_obj_malloc_with_finaliser(litert_compiled_model_obj_t, type);
    self->node = registry_add(&g_litert_models, raw_handle, global_ref);
    return MP_OBJ_FROM_PTR(self);
}

// Shared by create_input_buffers()/create_output_buffers() -- same
// bridge call shape, only the jmethodID differs (handled inside
// litert_jni_bridge.cpp, not here).
mp_obj_t create_buffers(litert_compiled_model_obj_t *self, bool is_output) {
    raise_if_model_closed(self);

    long *handles = nullptr;
    void **global_refs = nullptr;
    size_t count = 0;
    char *err = nullptr;
    bool ok = is_output
        ? litert_bridge_create_output_buffers(self->node->global_ref, &handles, &global_refs, &count, &err)
        : litert_bridge_create_input_buffers(self->node->global_ref, &handles, &global_refs, &count, &err);
    if (!ok) {
        raise_os_error_free(MP_EIO, err);
    }

    mp_obj_t *items = (mp_obj_t *) malloc(sizeof(mp_obj_t) * count);
    for (size_t i = 0; i < count; i++) {
        auto *buf = mp_obj_malloc_with_finaliser(litert_tensor_buffer_obj_t, &litert_tensor_buffer_type);
        buf->node = registry_add(&g_litert_buffers, handles[i], global_refs[i]);
        items[i] = MP_OBJ_FROM_PTR(buf);
    }
    mp_obj_t result = mp_obj_new_tuple(count, items);
    free(items);
    free(handles);
    free(global_refs);
    return result;
}

mp_obj_t litert_compiled_model_create_input_buffers(mp_obj_t self_in) {
    return create_buffers((litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in), false);
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_compiled_model_create_input_buffers_obj, litert_compiled_model_create_input_buffers);

mp_obj_t litert_compiled_model_create_output_buffers(mp_obj_t self_in) {
    return create_buffers((litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in), true);
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_compiled_model_create_output_buffers_obj, litert_compiled_model_create_output_buffers);

// Builds a raw_handle/global_ref array pair from a Python
// tuple/list of TensorBuffer objects. Caller frees both arrays.
void unpack_tensor_buffers(mp_obj_t seq, long **out_handles, void ***out_global_refs, size_t *out_count) {
    size_t count;
    mp_obj_t *items;
    mp_obj_get_array(seq, &count, &items);

    auto *handles = (long *) malloc(sizeof(long) * count);
    auto *global_refs = (void **) malloc(sizeof(void *) * count);
    for (size_t i = 0; i < count; i++) {
        if (!mp_obj_is_type(items[i], &litert_tensor_buffer_type)) {
            free(handles);
            free(global_refs);
            raise_os_error(MP_EINVAL, "android.litert: run() requires a sequence of TensorBuffer");
        }
        auto *buf = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(items[i]);
        if (!buf->node) {
            free(handles);
            free(global_refs);
            raise_os_error(MP_EINVAL, "android.litert: run() argument TensorBuffer is closed");
        }
        handles[i] = buf->node->raw_handle;
        global_refs[i] = buf->node->global_ref;
    }
    *out_handles = handles;
    *out_global_refs = global_refs;
    *out_count = count;
}

mp_obj_t litert_compiled_model_run(mp_obj_t self_in, mp_obj_t inputs_in, mp_obj_t outputs_in) {
    auto *self = (litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_model_closed(self);
    if (g_litert_backend == LitertBackend::kC) {
        raise_c_backend_not_implemented();
    }

    long *input_handles = nullptr, *output_handles = nullptr;
    void **input_refs = nullptr, **output_refs = nullptr;
    size_t num_inputs = 0, num_outputs = 0;
    unpack_tensor_buffers(inputs_in, &input_handles, &input_refs, &num_inputs);
    unpack_tensor_buffers(outputs_in, &output_handles, &output_refs, &num_outputs);

    char *err = nullptr;
    bool ok = litert_bridge_kotlin_run(self->node->global_ref, input_refs, num_inputs,
                                        output_refs, num_outputs, &err);

    free(input_handles);
    free(output_handles);
    free(input_refs);
    free(output_refs);

    if (!ok) {
        raise_os_error_free(MP_EIO, err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(litert_compiled_model_run_obj, litert_compiled_model_run);

void litert_compiled_model_close_impl(litert_compiled_model_obj_t *self) {
    if (!self->node) {
        return;
    }
    char *err = nullptr;
    LitertHandleNode *node = self->node;
    self->node = nullptr;
    if (!litert_bridge_close_compiled_model(node->global_ref, &err)) {
        registry_remove(&g_litert_models, node);
        raise_os_error_free(MP_EIO, err);
    }
    registry_remove(&g_litert_models, node);
}

mp_obj_t litert_compiled_model_close(mp_obj_t self_in) {
    litert_compiled_model_close_impl((litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_compiled_model_close_obj, litert_compiled_model_close);

mp_obj_t litert_compiled_model_del(mp_obj_t self_in) {
    litert_compiled_model_close_impl((litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_compiled_model_del_obj, litert_compiled_model_del);

const mp_rom_map_elem_t litert_compiled_model_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_create_input_buffers), MP_ROM_PTR(&litert_compiled_model_create_input_buffers_obj)},
    {MP_ROM_QSTR(MP_QSTR_create_output_buffers), MP_ROM_PTR(&litert_compiled_model_create_output_buffers_obj)},
    {MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&litert_compiled_model_run_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&litert_compiled_model_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&litert_compiled_model_del_obj)},
};
MP_DEFINE_CONST_DICT(litert_compiled_model_locals_dict, litert_compiled_model_locals_dict_table);

// ---------------- TensorBuffer ----------------
// No make_new -- only ever constructed internally (create_buffers()
// above), via CompiledModel.create_input_buffers()/
// create_output_buffers(), matching the real litert-api's own shape
// (TensorBuffer has no public constructor of its own either).

void raise_if_buffer_closed(litert_tensor_buffer_obj_t *self) {
    if (!self->node) {
        raise_os_error(MP_EINVAL, "android.litert: TensorBuffer closed");
    }
}

mp_obj_t litert_tensor_buffer_write_int8(mp_obj_t self_in, mp_obj_t data_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);
    if (g_litert_backend == LitertBackend::kC) {
        raise_c_backend_not_implemented();
    }

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    // No pre-check against a cached size here in v0 -- litert-api's own
    // TensorBuffer.writeInt8() validates length itself and raises its
    // own LiteRtException on mismatch, which the bridge's exception
    // handling already surfaces as an OSError below.

    char *err = nullptr;
    if (!litert_bridge_kotlin_write_int8(self->node->global_ref, (const int8_t *) bufinfo.buf,
                                          bufinfo.len, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_tensor_buffer_write_int8_obj, litert_tensor_buffer_write_int8);

mp_obj_t litert_tensor_buffer_read_int8(mp_obj_t self_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);
    if (g_litert_backend == LitertBackend::kC) {
        raise_c_backend_not_implemented();
    }

    int8_t *out = nullptr;
    size_t len = 0;
    char *err = nullptr;
    if (!litert_bridge_kotlin_read_int8(self->node->global_ref, &out, &len, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    mp_obj_t result = mp_obj_new_bytes((const byte *) out, len);
    free(out);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_tensor_buffer_read_int8_obj, litert_tensor_buffer_read_int8);

void litert_tensor_buffer_close_impl(litert_tensor_buffer_obj_t *self) {
    if (!self->node) {
        return;
    }
    char *err = nullptr;
    LitertHandleNode *node = self->node;
    self->node = nullptr;
    if (!litert_bridge_close_tensor_buffer(node->global_ref, &err)) {
        registry_remove(&g_litert_buffers, node);
        raise_os_error_free(MP_EIO, err);
    }
    registry_remove(&g_litert_buffers, node);
}

mp_obj_t litert_tensor_buffer_close(mp_obj_t self_in) {
    litert_tensor_buffer_close_impl((litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_tensor_buffer_close_obj, litert_tensor_buffer_close);

mp_obj_t litert_tensor_buffer_del(mp_obj_t self_in) {
    litert_tensor_buffer_close_impl((litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_tensor_buffer_del_obj, litert_tensor_buffer_del);

const mp_rom_map_elem_t litert_tensor_buffer_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_write_int8), MP_ROM_PTR(&litert_tensor_buffer_write_int8_obj)},
    {MP_ROM_QSTR(MP_QSTR_read_int8), MP_ROM_PTR(&litert_tensor_buffer_read_int8_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&litert_tensor_buffer_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&litert_tensor_buffer_del_obj)},
};
MP_DEFINE_CONST_DICT(litert_tensor_buffer_locals_dict, litert_tensor_buffer_locals_dict_table);

// ---------------- module-level: Accelerator ----------------
// set_backend()/get_backend() live outside the namespace, below --
// same "android_module.cpp needs external linkage" reasoning as the
// type objects above.

const mp_rom_map_elem_t litert_accelerator_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_Accelerator)},
    {MP_ROM_QSTR(MP_QSTR_NONE), MP_ROM_INT(0)},
    {MP_ROM_QSTR(MP_QSTR_CPU), MP_ROM_INT(1)},
    {MP_ROM_QSTR(MP_QSTR_GPU), MP_ROM_INT(2)},
    {MP_ROM_QSTR(MP_QSTR_NPU), MP_ROM_INT(3)},
};
MP_DEFINE_CONST_DICT(litert_accelerator_globals, litert_accelerator_globals_table);

} // namespace

// set_backend()/get_backend(): declared outside the anonymous namespace
// so android_module.cpp can reference the function objects, same
// linkage reasoning as android_rt_info_obj in rt_module.cpp.
mp_obj_t android_litert_set_backend(mp_obj_t backend_in) {
    const char *s = mp_obj_str_get_str(backend_in);
    if (strcmp(s, "c") == 0) {
        g_litert_backend = LitertBackend::kC;
    } else if (strcmp(s, "kotlin") == 0) {
        g_litert_backend = LitertBackend::kKotlin;
    } else {
        raise_os_error(MP_EINVAL, "android.litert: backend must be 'c' or 'kotlin'");
    }
    return mp_const_none;
}
extern MP_DEFINE_CONST_FUN_OBJ_1(android_litert_set_backend_obj, android_litert_set_backend);

mp_obj_t android_litert_get_backend() {
    const char *s = g_litert_backend == LitertBackend::kC ? "c" : "kotlin";
    return mp_obj_new_str(s, strlen(s));
}
extern MP_DEFINE_CONST_FUN_OBJ_0(android_litert_get_backend_obj, android_litert_get_backend);

extern MP_DEFINE_CONST_OBJ_TYPE(
    litert_options_type,
    MP_QSTR_Options,
    MP_TYPE_FLAG_NONE,
    make_new, litert_options_make_new
    );

extern MP_DEFINE_CONST_OBJ_TYPE(
    litert_environment_type,
    MP_QSTR_Environment,
    MP_TYPE_FLAG_NONE,
    make_new, litert_environment_make_new,
    locals_dict, &litert_environment_locals_dict
    );

extern MP_DEFINE_CONST_OBJ_TYPE(
    litert_compiled_model_type,
    MP_QSTR_CompiledModel,
    MP_TYPE_FLAG_NONE,
    make_new, litert_compiled_model_make_new,
    locals_dict, &litert_compiled_model_locals_dict
    );

extern MP_DEFINE_CONST_OBJ_TYPE(
    litert_tensor_buffer_type,
    MP_QSTR_TensorBuffer,
    MP_TYPE_FLAG_NONE,
    locals_dict, &litert_tensor_buffer_locals_dict
    );

// android_module.cpp nests this under android.litert.Accelerator.
extern const mp_obj_module_t litert_accelerator_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &litert_accelerator_globals,
};

extern "C" void litert_bridge_init(void *jni_env) {
    litert_bridge_init_impl(jni_env);
}

extern "C" void litert_close_all(void) {
    // Buffers and models first (independently script-visible, either
    // order is fine among themselves), environments last -- an
    // environment must outlive every compiled model created from it,
    // same reasoning as rt_close_all()'s own ordering.
    while (g_litert_buffers) {
        LitertHandleNode *node = g_litert_buffers;
        char *err = nullptr;
        litert_bridge_close_tensor_buffer(node->global_ref, &err);
        free(err);
        g_litert_buffers = node->next;
        free(node);
    }
    while (g_litert_models) {
        LitertHandleNode *node = g_litert_models;
        char *err = nullptr;
        litert_bridge_close_compiled_model(node->global_ref, &err);
        free(err);
        g_litert_models = node->next;
        free(node);
    }
    while (g_litert_environments) {
        LitertHandleNode *node = g_litert_environments;
        char *err = nullptr;
        litert_bridge_close_environment(node->global_ref, &err);
        free(err);
        g_litert_environments = node->next;
        free(node);
    }
}
