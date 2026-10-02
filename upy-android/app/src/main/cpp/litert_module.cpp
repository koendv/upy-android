// upy-android native litert module (top-level `litert`). Standalone
// LiteRT (not the Play-Store-delivered variant), CompiledModel.run()
// takes/returns array.array directly: a MicroPython builtin whose
// typecodes cover the real LiteRtElementType range this bridge
// supports.
//
// Zero cross-links with tflite_module.cpp: independent source,
// independent registry/teardown. The two modules share only the one
// upstream native library, libLiteRt.so -- see CMakeLists.txt's
// comment on tflite_module.cpp for why that's one copy of TFLite in
// the process, not two.
// see session-state: litert_module.cpp#module_design

#include <cstdlib>
#include <cstring>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/binary.h"
#include "py/mperrno.h"
}

#include "litert_jni_bridge.h"
#include "litert_module.h"

// see session-state: litert_module.cpp#module_design
#include "litert/c/litert_common.h"
#include "litert/c/litert_environment.h"
#include "litert/c/litert_model.h"
#include "litert/c/litert_model_types.h"

// see session-state: litert_module.cpp#module_design
extern const mp_obj_type_t litert_environment_type;
extern const mp_obj_type_t litert_compiled_model_type;
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
// describing a real Kotlin/JNI exception. Copied into a new MicroPython
// str (which copies internally) before being freed here.
void raise_os_error_free(int errno_, char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    free(msg);
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// see session-state: litert_module.cpp#registry_design
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

// Allocates a fresh, GC-owned array.array of the given typecode/length.
mp_obj_array_t *new_typed_array(char typecode, size_t len) {
    size_t itemsize = mp_binary_get_size('@', typecode, nullptr);
    auto *o = m_new_obj(mp_obj_array_t);
    o->base.type = &mp_type_array;
    o->typecode = typecode;
    o->free = 0;
    o->len = len;
    o->items = m_new(byte, itemsize * len);
    return o;
}

// ---------------- Environment ----------------

struct litert_environment_obj_t {
    mp_obj_base_t base;
    LitertHandleNode *node;  // null once closed
};

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

struct litert_options_obj_t {
    mp_obj_base_t base;
    mp_int_t accelerator;  // Accelerator.value: NONE=0, CPU=1, GPU=2, NPU=3
};

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

struct litert_compiled_model_obj_t {
    mp_obj_base_t base;
    LitertHandleNode *node;
    // strdup'd copy of the (already VFS-leading-slash-stripped) path
    // this model was constructed from. Used by find_tensor() below,
    // which reopens the same file directly via the plain C API
    // (LiteRtCreateModelFromFile) to find a tensor's dtype/shape --
    // Kotlin's CompiledModel has no such query of its own.
    char *path;
};

void raise_if_model_closed(litert_compiled_model_obj_t *self) {
    if (!self->node) {
        raise_os_error(MP_EINVAL, "litert: CompiledModel closed -- construct a new one");
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
        raise_os_error(MP_EINVAL, "litert: environment must be a litert.Environment");
    }
    auto *env_obj = (litert_environment_obj_t *) MP_OBJ_TO_PTR(parsed[ARG_environment].u_obj);
    if (!env_obj->node) {
        raise_os_error(MP_EINVAL, "litert: environment is closed");
    }

    const char *path = mp_obj_str_get_str(parsed[ARG_path].u_obj);
    // CompiledModel.create() -> LiteRtCreateModelFromFile is not
    // VFS-aware (it's a real fopen() underneath, on the raw filesystem,
    // not this port's own VfsPosix-mounted "/"). Strip the leading
    // '/' so a VFS-absolute path resolves relative to cwd (the app's
    // own private storage root) instead of the real device filesystem
    // root.
    if (path[0] == '/') {
        path++;
    }

    mp_int_t accelerator_value = 1;  // Accelerator.CPU
    if (parsed[ARG_options].u_obj != MP_OBJ_NULL) {
        if (!mp_obj_is_type(parsed[ARG_options].u_obj, &litert_options_type)) {
            raise_os_error(MP_EINVAL, "litert: options must be a litert.Options");
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
    self->path = strdup(path);
    return MP_OBJ_FROM_PTR(self);
}

// Finds tensor `index` (input or output) by reopening the model file
// via the plain LiteRt C API (cheap: flatbuffer metadata only, no
// interpreter/arena) -- Kotlin's CompiledModel has no "what dtype/shape
// is tensor N" query of its own. Caller must LiteRtDestroyModel/
// LiteRtDestroyEnvironment once done with *out_tensor.
void find_tensor(litert_compiled_model_obj_t *self, bool is_output, mp_int_t index,
                  LiteRtEnvironment *out_environment, LiteRtModel *out_model, LiteRtTensor *out_tensor) {
    LiteRtEnvironment environment = nullptr;
    if (LiteRtCreateEnvironment(0, nullptr, &environment) != kLiteRtStatusOk) {
        raise_os_error(MP_EIO, "litert: failed to create environment for tensor introspection");
    }
    LiteRtModel model = nullptr;
    if (LiteRtCreateModelFromFile(environment, self->path, &model) != kLiteRtStatusOk) {
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EIO, "litert: failed to open model file for tensor introspection");
    }
    LiteRtSignature signature = nullptr;
    if (LiteRtGetModelSignature(model, 0, &signature) != kLiteRtStatusOk) {
        LiteRtDestroyModel(model);
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EIO, "litert: model has no signatures");
    }
    LiteRtTensor tensor = nullptr;
    LiteRtStatus status = is_output
        ? LiteRtGetSignatureOutputTensorByIndex(signature, (LiteRtParamIndex) index, &tensor)
        : LiteRtGetSignatureInputTensorByIndex(signature, (LiteRtParamIndex) index, &tensor);
    if (status != kLiteRtStatusOk) {
        LiteRtDestroyModel(model);
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EINVAL, is_output ? "litert: no such output tensor index" : "litert: no such input tensor index");
    }
    *out_environment = environment;
    *out_model = model;
    *out_tensor = tensor;
}

// Dtype only, no shape tuple -- run()'s own dispatch needs just this,
// not the input_shape()/output_shape() methods' full shape build.
LiteRtElementType tensor_element_type(litert_compiled_model_obj_t *self, bool is_output, mp_int_t index) {
    LiteRtEnvironment environment;
    LiteRtModel model;
    LiteRtTensor tensor;
    find_tensor(self, is_output, index, &environment, &model, &tensor);

    LiteRtTensorTypeId type_id;
    if (LiteRtGetTensorTypeId(tensor, &type_id) != kLiteRtStatusOk) {
        LiteRtDestroyModel(model);
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EIO, "litert: failed to get tensor type id");
    }
    LiteRtElementType element_type;
    if (type_id == kLiteRtUnrankedTensorType) {
        LiteRtUnrankedTensorType unranked;
        if (LiteRtGetUnrankedTensorType(tensor, &unranked) != kLiteRtStatusOk) {
            LiteRtDestroyModel(model);
            LiteRtDestroyEnvironment(environment);
            raise_os_error(MP_EIO, "litert: failed to get unranked tensor type");
        }
        element_type = unranked.element_type;
    } else {
        LiteRtRankedTensorType ranked;
        if (LiteRtGetRankedTensorType(tensor, &ranked) != kLiteRtStatusOk) {
            LiteRtDestroyModel(model);
            LiteRtDestroyEnvironment(environment);
            raise_os_error(MP_EIO, "litert: failed to get ranked tensor type");
        }
        element_type = ranked.element_type;
    }
    LiteRtDestroyModel(model);
    LiteRtDestroyEnvironment(environment);
    return element_type;
}

// Shape tuple, for input_shape()/output_shape() below.
mp_obj_t tensor_shape(litert_compiled_model_obj_t *self, bool is_output, mp_int_t index) {
    LiteRtEnvironment environment;
    LiteRtModel model;
    LiteRtTensor tensor;
    find_tensor(self, is_output, index, &environment, &model, &tensor);

    LiteRtTensorTypeId type_id;
    if (LiteRtGetTensorTypeId(tensor, &type_id) != kLiteRtStatusOk) {
        LiteRtDestroyModel(model);
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EIO, "litert: failed to get tensor type id");
    }
    mp_obj_t shape;
    if (type_id == kLiteRtUnrankedTensorType) {
        shape = mp_const_none;
    } else {
        LiteRtRankedTensorType ranked;
        if (LiteRtGetRankedTensorType(tensor, &ranked) != kLiteRtStatusOk) {
            LiteRtDestroyModel(model);
            LiteRtDestroyEnvironment(environment);
            raise_os_error(MP_EIO, "litert: failed to get ranked tensor type");
        }
        unsigned int rank = ranked.layout.rank;
        mp_obj_t *dims = m_new(mp_obj_t, rank);
        for (unsigned int i = 0; i < rank; i++) {
            dims[i] = mp_obj_new_int(ranked.layout.dimensions[i]);
        }
        shape = mp_obj_new_tuple(rank, dims);
    }
    LiteRtDestroyModel(model);
    LiteRtDestroyEnvironment(environment);
    return shape;
}

mp_obj_t litert_compiled_model_input_shape(mp_obj_t self_in, mp_obj_t index_in) {
    auto *self = (litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_model_closed(self);
    return tensor_shape(self, false, mp_obj_get_int(index_in));
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_compiled_model_input_shape_obj, litert_compiled_model_input_shape);

mp_obj_t litert_compiled_model_output_shape(mp_obj_t self_in, mp_obj_t index_in) {
    auto *self = (litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_model_closed(self);
    return tensor_shape(self, true, mp_obj_get_int(index_in));
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_compiled_model_output_shape_obj, litert_compiled_model_output_shape);

// A set of TensorBuffers created via create_input_buffers()/
// create_output_buffers() -- internal plumbing only in this version,
// never a Python-visible object (the old TensorBuffer type is gone).
// A run() call creates one, uses it, and closes it; no reuse across
// calls in v1 (see session-state).
struct BufferSet {
    LitertHandleNode **nodes;
    size_t count;
};

BufferSet create_buffer_set(litert_compiled_model_obj_t *self, bool is_output) {
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
    auto **nodes = (LitertHandleNode **) malloc(sizeof(LitertHandleNode *) * count);
    for (size_t i = 0; i < count; i++) {
        nodes[i] = registry_add(&g_litert_buffers, handles[i], global_refs[i]);
    }
    free(handles);
    free(global_refs);
    return {nodes, count};
}

void close_buffer_set(BufferSet &bufs) {
    for (size_t i = 0; i < bufs.count; i++) {
        if (!bufs.nodes[i]) {
            continue;
        }
        char *err = nullptr;
        litert_bridge_close_tensor_buffer(bufs.nodes[i]->global_ref, &err);
        free(err);  // best-effort: a close failure here isn't raised, same as litert_close_all()'s own discipline
        registry_remove(&g_litert_buffers, bufs.nodes[i]);
    }
    free(bufs.nodes);
}

// Writes an array.array's buffer into an input TensorBuffer, via
// whichever typed bridge call matches the array's typecode. No
// quantization/scale/zero_point handling -- no implicit conversion
// (see session-state). bool tensors take a uint8 ('B') array of 0/1
// bytes, reinterpreted as C++ bool: safe on this target specifically
// (sizeof(bool) == 1 on arm64 clang), not a portable assumption in
// general.
void write_array_to_buffer(LitertHandleNode *node, mp_obj_t arr_obj) {
    if (!mp_obj_is_type(arr_obj, &mp_type_array)) {
        raise_os_error(MP_EINVAL, "litert: run() arguments must be array.array");
    }
    mp_buffer_info_t bufinfo;
    // mp_get_buffer_raise rejects non-contiguous views -- a success
    // here IS the "safe to hand this pointer to litert" signal.
    mp_get_buffer_raise(arr_obj, &bufinfo, MP_BUFFER_READ);

    char *err = nullptr;
    bool ok;
    switch (bufinfo.typecode) {
        case 'f':
            ok = litert_bridge_kotlin_write_float(node->global_ref, (const float *) bufinfo.buf,
                bufinfo.len / sizeof(float), &err);
            break;
        case 'b':
            ok = litert_bridge_kotlin_write_int8(node->global_ref, (const int8_t *) bufinfo.buf, bufinfo.len, &err);
            break;
        case 'i':
            ok = litert_bridge_kotlin_write_int(node->global_ref, (const int32_t *) bufinfo.buf,
                bufinfo.len / sizeof(int32_t), &err);
            break;
        case 'q':
            ok = litert_bridge_kotlin_write_long(node->global_ref, (const int64_t *) bufinfo.buf,
                bufinfo.len / sizeof(int64_t), &err);
            break;
        case 'B':
            ok = litert_bridge_kotlin_write_bool(node->global_ref, (const bool *) bufinfo.buf, bufinfo.len, &err);
            break;
        default:
            raise_os_error(MP_EINVAL, "litert: input dtype not supported in this version (only f/b/i/q/B typecodes)");
    }
    // litert-api's own TensorBuffer.write*() validates length itself
    // and raises its own LiteRtException on a size mismatch, surfaced
    // here as an OSError -- no separate pre-check needed.
    if (!ok) {
        raise_os_error_free(MP_EIO, err);
    }
}

// Reads output TensorBuffer `index` back as a freshly allocated,
// GC-owned, flat (1-D) array.array. Always a copy in v1. Dtype
// dispatch mirrors write_array_to_buffer above.
mp_obj_t read_buffer_as_array(litert_compiled_model_obj_t *self, LitertHandleNode *node, mp_int_t index) {
    LiteRtElementType element_type = tensor_element_type(self, true, index);
    char *err = nullptr;

    if (element_type == kLiteRtElementTypeFloat32) {
        float *out = nullptr;
        size_t len = 0;
        if (!litert_bridge_kotlin_read_float(node->global_ref, &out, &len, &err)) {
            raise_os_error_free(MP_EIO, err);
        }
        mp_obj_array_t *arr = new_typed_array('f', len);
        memcpy(arr->items, out, len * sizeof(float));
        free(out);
        return MP_OBJ_FROM_PTR(arr);
    }
    if (element_type == kLiteRtElementTypeInt8) {
        int8_t *out = nullptr;
        size_t len = 0;
        if (!litert_bridge_kotlin_read_int8(node->global_ref, &out, &len, &err)) {
            raise_os_error_free(MP_EIO, err);
        }
        mp_obj_array_t *arr = new_typed_array('b', len);
        memcpy(arr->items, out, len);
        free(out);
        return MP_OBJ_FROM_PTR(arr);
    }
    if (element_type == kLiteRtElementTypeInt32) {
        int32_t *out = nullptr;
        size_t len = 0;
        if (!litert_bridge_kotlin_read_int(node->global_ref, &out, &len, &err)) {
            raise_os_error_free(MP_EIO, err);
        }
        mp_obj_array_t *arr = new_typed_array('i', len);
        memcpy(arr->items, out, len * sizeof(int32_t));
        free(out);
        return MP_OBJ_FROM_PTR(arr);
    }
    if (element_type == kLiteRtElementTypeInt64) {
        int64_t *out = nullptr;
        size_t len = 0;
        if (!litert_bridge_kotlin_read_long(node->global_ref, &out, &len, &err)) {
            raise_os_error_free(MP_EIO, err);
        }
        mp_obj_array_t *arr = new_typed_array('q', len);
        memcpy(arr->items, out, len * sizeof(int64_t));
        free(out);
        return MP_OBJ_FROM_PTR(arr);
    }
    if (element_type == kLiteRtElementTypeBool) {
        bool *out = nullptr;
        size_t len = 0;
        if (!litert_bridge_kotlin_read_bool(node->global_ref, &out, &len, &err)) {
            raise_os_error_free(MP_EIO, err);
        }
        mp_obj_array_t *arr = new_typed_array('B', len);
        memcpy(arr->items, out, len);  // sizeof(bool) == 1 on this target -- see write_array_to_buffer's own note
        free(out);
        return MP_OBJ_FROM_PTR(arr);
    }
    raise_os_error(MP_EINVAL, "litert: output dtype not supported in this version (only float32/int8/int32/int64/bool)");
    return mp_const_none;  // unreachable: raise_os_error() never returns
}

// run(*inputs): one positional array.array per input tensor, in the
// model's own order. Returns a single array.array if the model has
// one output tensor, otherwise a tuple.
mp_obj_t litert_compiled_model_run(size_t n_args, const mp_obj_t *args) {
    auto *self = (litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(args[0]);
    raise_if_model_closed(self);

    BufferSet inputs = create_buffer_set(self, false);
    BufferSet outputs = create_buffer_set(self, true);

    if (inputs.count != n_args - 1) {
        size_t want = inputs.count;
        close_buffer_set(inputs);
        close_buffer_set(outputs);
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("litert: run() needs %d input(s), got %d"),
            (int) want, (int) (n_args - 1));
    }

    for (size_t i = 0; i < inputs.count; i++) {
        write_array_to_buffer(inputs.nodes[i], args[1 + i]);
    }

    auto *input_refs = (void **) malloc(sizeof(void *) * inputs.count);
    auto *output_refs = (void **) malloc(sizeof(void *) * outputs.count);
    for (size_t i = 0; i < inputs.count; i++) {
        input_refs[i] = inputs.nodes[i]->global_ref;
    }
    for (size_t i = 0; i < outputs.count; i++) {
        output_refs[i] = outputs.nodes[i]->global_ref;
    }

    char *err = nullptr;
    bool ok = litert_bridge_kotlin_run(self->node->global_ref, input_refs, inputs.count,
                                        output_refs, outputs.count, &err);
    free(input_refs);
    free(output_refs);
    if (!ok) {
        close_buffer_set(inputs);
        close_buffer_set(outputs);
        raise_os_error_free(MP_EIO, err);
    }

    mp_obj_t result;
    if (outputs.count == 1) {
        result = read_buffer_as_array(self, outputs.nodes[0], 0);
    } else {
        mp_obj_t *items = m_new(mp_obj_t, outputs.count);
        for (size_t i = 0; i < outputs.count; i++) {
            items[i] = read_buffer_as_array(self, outputs.nodes[i], (mp_int_t) i);
        }
        result = mp_obj_new_tuple(outputs.count, items);
    }

    close_buffer_set(inputs);
    close_buffer_set(outputs);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(litert_compiled_model_run_obj, 1, MP_OBJ_FUN_ARGS_MAX, litert_compiled_model_run);

void litert_compiled_model_close_impl(litert_compiled_model_obj_t *self) {
    if (!self->node) {
        return;
    }
    free(self->path);
    self->path = nullptr;
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
    {MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&litert_compiled_model_run_obj)},
    {MP_ROM_QSTR(MP_QSTR_input_shape), MP_ROM_PTR(&litert_compiled_model_input_shape_obj)},
    {MP_ROM_QSTR(MP_QSTR_output_shape), MP_ROM_PTR(&litert_compiled_model_output_shape_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&litert_compiled_model_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&litert_compiled_model_del_obj)},
};
MP_DEFINE_CONST_DICT(litert_compiled_model_locals_dict, litert_compiled_model_locals_dict_table);

// ---------------- module-level: Accelerator ----------------

const mp_rom_map_elem_t litert_accelerator_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_Accelerator)},
    {MP_ROM_QSTR(MP_QSTR_NONE), MP_ROM_INT(0)},
    {MP_ROM_QSTR(MP_QSTR_CPU), MP_ROM_INT(1)},
    {MP_ROM_QSTR(MP_QSTR_GPU), MP_ROM_INT(2)},
    {MP_ROM_QSTR(MP_QSTR_NPU), MP_ROM_INT(3)},
};
MP_DEFINE_CONST_DICT(litert_accelerator_globals, litert_accelerator_globals_table);

const mp_obj_module_t litert_accelerator_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &litert_accelerator_globals,
};

// Runtime-queryable usage reference for an AI (or human) driving this
// module blind over adb, with no repo access -- see
// AdbExecProvider.kt/adb_help.yaml (help -> help('modules') -> import
// litert; print(litert.help())).
const char litert_help_text[] =
    "module: litert (standalone LiteRT, not the Play-Store-delivered variant)\n"
    "classes:\n"
    "  litert.Environment(): required once per Environment() instance passed to CompiledModel()\n"
    "  litert.Options(accelerator=litert.Accelerator.CPU): accelerator is litert.Accelerator.{NONE,CPU,GPU,NPU}\n"
    "  litert.CompiledModel(environment, path, options=None): loads and compiles a .tflite model; path is a VFS path\n"
    "methods (CompiledModel):\n"
    "  run(*inputs): one positional array.array per input tensor, in the model's own order\n"
    "    strict: each array's typecode must be one of f/b/i/q/B (float32/int8/int32/int64/bool); no quantization/scale/zero_point handling, no implicit cast\n"
    "    returns a single array.array if the model has one output tensor, otherwise a tuple of array.array; always flat (1-D), always a fresh copy\n"
    "  input_shape(i)/output_shape(i): tuple of dimensions for input/output tensor i; run()'s own arrays carry no shape, query it here\n"
    "  close(): frees the model; also called automatically on garbage collection\n"
    "dtypes_supported: float32('f'), int8('b'), int32('i'), int64('q'), bool (as uint8 'B', 0/1) -- the Kotlin litert-api itself has no uint8/int16/uint16 path, unlike tflite\n"
    "errors:\n"
    "  OSError(EINVAL): construction with a closed/wrong-type Environment or Options, a call on a closed CompiledModel, a bad tensor index, or a dtype outside dtypes_supported\n"
    "  OSError(EIO): compile, buffer creation, write, run, or read failure (Kotlin/JNI exception text is forwarded)\n"
    "  ValueError: wrong number of run() arguments\n"
    "notes:\n"
    "  accelerator-capable (CPU/GPU/NPU via litert.Accelerator) -- see tflite.help() for a plain CPU-only, non-Kotlin alternative\n"
    "see_also: tflite.help()\n"
;
mp_obj_t litert_help() {
    return mp_obj_new_str(litert_help_text, strlen(litert_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(litert_help_obj, litert_help);

const mp_rom_map_elem_t litert_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_litert)},
    {MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&litert_help_obj)},
    {MP_ROM_QSTR(MP_QSTR_Environment), MP_ROM_PTR(&litert_environment_type)},
    {MP_ROM_QSTR(MP_QSTR_CompiledModel), MP_ROM_PTR(&litert_compiled_model_type)},
    {MP_ROM_QSTR(MP_QSTR_Options), MP_ROM_PTR(&litert_options_type)},
    {MP_ROM_QSTR(MP_QSTR_Accelerator), MP_ROM_PTR(&litert_accelerator_module)},
};
MP_DEFINE_CONST_DICT(litert_module_globals, litert_module_globals_table);

} // namespace

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

// Top-level module, matches a real/expected module name, same tier as
// ulab/image, not android-specific glue.
extern "C" const mp_obj_module_t litert_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &litert_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_litert, litert_module);

extern "C" void litert_bridge_init(void *jni_env) {
    litert_bridge_init_impl(jni_env);
}

extern "C" void litert_close_all(void) {
    // Buffers and models first (independently script-visible, either
    // order is fine among themselves), environments last. An
    // environment must outlive every compiled model created from it.
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
