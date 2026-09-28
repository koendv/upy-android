// upy-android native litert module (top-level `litert`).
// micropython android port only.
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

// see session-state: litert_module.cpp#module_design
#include "litert/c/litert_common.h"
#include "litert/c/litert_environment.h"
#include "litert/c/litert_model.h"
#include "litert/c/litert_model_types.h"

// see session-state: litert_module.cpp#module_design
extern const mp_obj_type_t litert_environment_type;
extern const mp_obj_type_t litert_compiled_model_type;
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

struct litert_environment_obj_t {
    mp_obj_base_t base;
    LitertHandleNode *node;  // null once closed
};

struct litert_compiled_model_obj_t {
    mp_obj_base_t base;
    LitertHandleNode *node;
    // strdup'd copy of the (already VFS-leading-slash-stripped) path this
    // model was constructed from. Only used by get_input_tensor_type()/
    // get_output_tensor_type()/get_input_tensor_quantization()/
    // get_output_tensor_quantization() below, which reopen the same file
    // directly via the plain C API (LiteRtCreateModelFromFile) rather
    // than asking Kotlin's already-loaded CompiledModel for it.
    char *path;
};

struct litert_tensor_buffer_obj_t {
    mp_obj_base_t base;
    LitertHandleNode *node;
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

// Shared by create_input_buffers()/create_output_buffers(). Same
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
            raise_os_error(MP_EINVAL, "litert: run() requires a sequence of TensorBuffer");
        }
        auto *buf = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(items[i]);
        if (!buf->node) {
            free(handles);
            free(global_refs);
            raise_os_error(MP_EINVAL, "litert: run() argument TensorBuffer is closed");
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

// ---------------- CompiledModel tensor introspection ----------------
// see session-state: litert_module.cpp#module_design

// see session-state: litert_module.cpp#find_tensor_or_raise
void find_tensor_or_raise(const char *path, bool is_output, mp_int_t index,
                           LiteRtEnvironment *out_environment, LiteRtModel *out_model,
                           LiteRtTensor *out_tensor) {
    LiteRtEnvironment environment = nullptr;
    if (LiteRtCreateEnvironment(0, nullptr, &environment) != kLiteRtStatusOk) {
        raise_os_error(MP_EIO, "litert: failed to create environment for tensor introspection");
    }

    LiteRtModel model = nullptr;
    if (LiteRtCreateModelFromFile(environment, path, &model) != kLiteRtStatusOk) {
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EIO, "litert: failed to open model file for tensor introspection");
    }

    LiteRtSignature signature = nullptr;
    if (LiteRtGetModelSignature(model, 0, &signature) != kLiteRtStatusOk) {
        LiteRtDestroyModel(model);
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EIO, "litert: model has no signatures");
    }

    if (index < 0) {
        LiteRtDestroyModel(model);
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EINVAL, "litert: tensor index must not be negative");
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

const char *dtype_name_for_element_type(LiteRtElementType element_type) {
    switch (element_type) {
        case kLiteRtElementTypeBool: return "bool";
        case kLiteRtElementTypeInt8: return "int8";
        case kLiteRtElementTypeInt16: return "int16";
        case kLiteRtElementTypeInt32: return "int32";
        case kLiteRtElementTypeInt64: return "int64";
        case kLiteRtElementTypeUInt8: return "uint8";
        case kLiteRtElementTypeUInt16: return "uint16";
        case kLiteRtElementTypeUInt32: return "uint32";
        case kLiteRtElementTypeUInt64: return "uint64";
        case kLiteRtElementTypeFloat16: return "float16";
        case kLiteRtElementTypeFloat32: return "float32";
        case kLiteRtElementTypeFloat64: return "float64";
        default: return "unknown";
    }
}

// Returns (shape_tuple_or_None, dtype_str). shape is None only for the
// rare unranked-tensor case (real .tflite models are ranked in practice).
// Cleans up model/environment itself before any raise, same discipline
// as find_tensor_or_raise() above.
mp_obj_t tensor_type_to_mp_obj(LiteRtEnvironment environment, LiteRtModel model, LiteRtTensor tensor) {
    LiteRtTensorTypeId type_id;
    if (LiteRtGetTensorTypeId(tensor, &type_id) != kLiteRtStatusOk) {
        LiteRtDestroyModel(model);
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EIO, "litert: failed to get tensor type id");
    }

    if (type_id == kLiteRtUnrankedTensorType) {
        LiteRtUnrankedTensorType unranked;
        if (LiteRtGetUnrankedTensorType(tensor, &unranked) != kLiteRtStatusOk) {
            LiteRtDestroyModel(model);
            LiteRtDestroyEnvironment(environment);
            raise_os_error(MP_EIO, "litert: failed to get unranked tensor type");
        }
        const char *dtype = dtype_name_for_element_type(unranked.element_type);
        mp_obj_t items[2] = {mp_const_none, mp_obj_new_str(dtype, strlen(dtype))};
        return mp_obj_new_tuple(2, items);
    }

    LiteRtRankedTensorType ranked;
    if (LiteRtGetRankedTensorType(tensor, &ranked) != kLiteRtStatusOk) {
        LiteRtDestroyModel(model);
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EIO, "litert: failed to get ranked tensor type");
    }
    unsigned int rank = ranked.layout.rank;
    auto *dims = (mp_obj_t *) malloc(sizeof(mp_obj_t) * rank);
    for (unsigned int i = 0; i < rank; i++) {
        dims[i] = mp_obj_new_int(ranked.layout.dimensions[i]);
    }
    mp_obj_t shape = mp_obj_new_tuple(rank, dims);
    free(dims);
    const char *dtype = dtype_name_for_element_type(ranked.element_type);
    mp_obj_t items[2] = {shape, mp_obj_new_str(dtype, strlen(dtype))};
    return mp_obj_new_tuple(2, items);
}

// Returns a tuple whose first element is always a scheme-tag string,
// followed by scheme-specific fields (no shared shape across schemes,
// callers destructure the tag first):
//   ("none",)
//   ("per_tensor", scale: float, zero_point: int)
//   ("per_channel", quantized_dimension: int, scales: tuple[float, ...], zero_points: tuple[int, ...])
//   ("block_wise", block_size: int). LiteRT itself has no models
//   producing this yet (the schema is marked "not implemented" upstream);
//   included so callers can already handle it instead of being surprised
//   later.
// Cleans up model/environment itself before any raise, same discipline
// as find_tensor_or_raise()/tensor_type_to_mp_obj() above.
mp_obj_t quantization_to_mp_obj(LiteRtEnvironment environment, LiteRtModel model, LiteRtTensor tensor) {
    LiteRtQuantizationTypeId type_id;
    if (LiteRtGetQuantizationTypeId(tensor, &type_id) != kLiteRtStatusOk) {
        LiteRtDestroyModel(model);
        LiteRtDestroyEnvironment(environment);
        raise_os_error(MP_EIO, "litert: failed to get quantization type id");
    }

    switch (type_id) {
        case kLiteRtQuantizationNone: {
            mp_obj_t items[1] = {mp_obj_new_str("none", 4)};
            return mp_obj_new_tuple(1, items);
        }
        case kLiteRtQuantizationPerTensor: {
            LiteRtQuantizationPerTensor per_tensor;
            if (LiteRtGetPerTensorQuantization(tensor, &per_tensor) != kLiteRtStatusOk) {
                LiteRtDestroyModel(model);
                LiteRtDestroyEnvironment(environment);
                raise_os_error(MP_EIO, "litert: failed to get per-tensor quantization");
            }
            mp_obj_t items[3] = {
                mp_obj_new_str("per_tensor", 10),
                mp_obj_new_float(per_tensor.scale),
                mp_obj_new_int_from_ll(per_tensor.zero_point),
            };
            return mp_obj_new_tuple(3, items);
        }
        case kLiteRtQuantizationPerChannel: {
            LiteRtQuantizationPerChannel per_channel;
            if (LiteRtGetPerChannelQuantization(tensor, &per_channel) != kLiteRtStatusOk) {
                LiteRtDestroyModel(model);
                LiteRtDestroyEnvironment(environment);
                raise_os_error(MP_EIO, "litert: failed to get per-channel quantization");
            }
            auto *scale_items = (mp_obj_t *) malloc(sizeof(mp_obj_t) * per_channel.num_channels);
            auto *zp_items = (mp_obj_t *) malloc(sizeof(mp_obj_t) * per_channel.num_channels);
            for (uint64_t i = 0; i < per_channel.num_channels; i++) {
                scale_items[i] = mp_obj_new_float(per_channel.scales[i]);
                zp_items[i] = mp_obj_new_int_from_ll(per_channel.zero_points[i]);
            }
            mp_obj_t scales = mp_obj_new_tuple(per_channel.num_channels, scale_items);
            mp_obj_t zero_points = mp_obj_new_tuple(per_channel.num_channels, zp_items);
            free(scale_items);
            free(zp_items);
            mp_obj_t items[4] = {
                mp_obj_new_str("per_channel", 11),
                mp_obj_new_int(per_channel.quantized_dimension),
                scales,
                zero_points,
            };
            return mp_obj_new_tuple(4, items);
        }
        case kLiteRtQuantizationBlockWise: {
            LiteRtQuantizationBlockWise block_wise;
            if (LiteRtGetBlockWiseQuantization(tensor, &block_wise) != kLiteRtStatusOk) {
                LiteRtDestroyModel(model);
                LiteRtDestroyEnvironment(environment);
                raise_os_error(MP_EIO, "litert: failed to get block-wise quantization");
            }
            mp_obj_t items[2] = {
                mp_obj_new_str("block_wise", 10),
                mp_obj_new_int(block_wise.block_size),
            };
            return mp_obj_new_tuple(2, items);
        }
    }
    LiteRtDestroyModel(model);
    LiteRtDestroyEnvironment(environment);
    raise_os_error(MP_EIO, "litert: unsupported quantization type id");
    return mp_const_none;  // unreachable: raise_os_error() never returns
}

mp_obj_t litert_compiled_model_get_tensor_type(litert_compiled_model_obj_t *self, mp_obj_t index_in, bool is_output) {
    raise_if_model_closed(self);
    mp_int_t index = mp_obj_get_int(index_in);

    LiteRtEnvironment environment;
    LiteRtModel model;
    LiteRtTensor tensor;
    find_tensor_or_raise(self->path, is_output, index, &environment, &model, &tensor);

    mp_obj_t result = tensor_type_to_mp_obj(environment, model, tensor);
    LiteRtDestroyModel(model);
    LiteRtDestroyEnvironment(environment);
    return result;
}

mp_obj_t litert_compiled_model_get_input_tensor_type(mp_obj_t self_in, mp_obj_t index_in) {
    return litert_compiled_model_get_tensor_type((litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in), index_in, false);
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_compiled_model_get_input_tensor_type_obj, litert_compiled_model_get_input_tensor_type);

mp_obj_t litert_compiled_model_get_output_tensor_type(mp_obj_t self_in, mp_obj_t index_in) {
    return litert_compiled_model_get_tensor_type((litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in), index_in, true);
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_compiled_model_get_output_tensor_type_obj, litert_compiled_model_get_output_tensor_type);

mp_obj_t litert_compiled_model_get_tensor_quantization(litert_compiled_model_obj_t *self, mp_obj_t index_in, bool is_output) {
    raise_if_model_closed(self);
    mp_int_t index = mp_obj_get_int(index_in);

    LiteRtEnvironment environment;
    LiteRtModel model;
    LiteRtTensor tensor;
    find_tensor_or_raise(self->path, is_output, index, &environment, &model, &tensor);

    mp_obj_t result = quantization_to_mp_obj(environment, model, tensor);
    LiteRtDestroyModel(model);
    LiteRtDestroyEnvironment(environment);
    return result;
}

mp_obj_t litert_compiled_model_get_input_tensor_quantization(mp_obj_t self_in, mp_obj_t index_in) {
    return litert_compiled_model_get_tensor_quantization((litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in), index_in, false);
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_compiled_model_get_input_tensor_quantization_obj, litert_compiled_model_get_input_tensor_quantization);

mp_obj_t litert_compiled_model_get_output_tensor_quantization(mp_obj_t self_in, mp_obj_t index_in) {
    return litert_compiled_model_get_tensor_quantization((litert_compiled_model_obj_t *) MP_OBJ_TO_PTR(self_in), index_in, true);
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_compiled_model_get_output_tensor_quantization_obj, litert_compiled_model_get_output_tensor_quantization);

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
    {MP_ROM_QSTR(MP_QSTR_create_input_buffers), MP_ROM_PTR(&litert_compiled_model_create_input_buffers_obj)},
    {MP_ROM_QSTR(MP_QSTR_create_output_buffers), MP_ROM_PTR(&litert_compiled_model_create_output_buffers_obj)},
    {MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&litert_compiled_model_run_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_input_tensor_type), MP_ROM_PTR(&litert_compiled_model_get_input_tensor_type_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_output_tensor_type), MP_ROM_PTR(&litert_compiled_model_get_output_tensor_type_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_input_tensor_quantization), MP_ROM_PTR(&litert_compiled_model_get_input_tensor_quantization_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_output_tensor_quantization), MP_ROM_PTR(&litert_compiled_model_get_output_tensor_quantization_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&litert_compiled_model_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&litert_compiled_model_del_obj)},
};
MP_DEFINE_CONST_DICT(litert_compiled_model_locals_dict, litert_compiled_model_locals_dict_table);

// ---------------- TensorBuffer ----------------
// No make_new. Only ever constructed internally (create_buffers()
// above), via CompiledModel.create_input_buffers()/
// create_output_buffers(), matching the real litert-api's own shape
// (TensorBuffer has no public constructor of its own either).

void raise_if_buffer_closed(litert_tensor_buffer_obj_t *self) {
    if (!self->node) {
        raise_os_error(MP_EINVAL, "litert: TensorBuffer closed");
    }
}

mp_obj_t litert_tensor_buffer_write_int8(mp_obj_t self_in, mp_obj_t data_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    // No pre-check against a cached size here. litert-api's own
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

// write_float/read_float, write_int/read_int, write_bool/read_bool,
// write_long/read_long: same shape as write_int8/read_int8 above, one
// real typed TensorBuffer method each (writeFloat/readFloat/writeInt/
// readInt/writeBoolean/readBoolean/writeLong/readLong, confirmed via
// javap). Takes/returns a plain MicroPython sequence/tuple of the
// matching Python type, not bytes: these are typed arrays on the
// Kotlin side, not raw byte buffers.

mp_obj_t litert_tensor_buffer_write_float(mp_obj_t self_in, mp_obj_t data_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);

    size_t count;
    mp_obj_t *items;
    mp_obj_get_array(data_in, &count, &items);
    auto *raw = (float *) malloc(sizeof(float) * count);
    for (size_t i = 0; i < count; i++) {
        raw[i] = (float) mp_obj_get_float(items[i]);
    }
    char *err = nullptr;
    bool ok = litert_bridge_kotlin_write_float(self->node->global_ref, raw, count, &err);
    free(raw);
    if (!ok) {
        raise_os_error_free(MP_EIO, err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_tensor_buffer_write_float_obj, litert_tensor_buffer_write_float);

mp_obj_t litert_tensor_buffer_read_float(mp_obj_t self_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);

    float *out = nullptr;
    size_t len = 0;
    char *err = nullptr;
    if (!litert_bridge_kotlin_read_float(self->node->global_ref, &out, &len, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    mp_obj_t *items = (mp_obj_t *) malloc(sizeof(mp_obj_t) * len);
    for (size_t i = 0; i < len; i++) {
        items[i] = mp_obj_new_float(out[i]);
    }
    mp_obj_t result = mp_obj_new_tuple(len, items);
    free(items);
    free(out);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_tensor_buffer_read_float_obj, litert_tensor_buffer_read_float);

mp_obj_t litert_tensor_buffer_write_int(mp_obj_t self_in, mp_obj_t data_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);

    size_t count;
    mp_obj_t *items;
    mp_obj_get_array(data_in, &count, &items);
    auto *raw = (int32_t *) malloc(sizeof(int32_t) * count);
    for (size_t i = 0; i < count; i++) {
        raw[i] = (int32_t) mp_obj_get_int(items[i]);
    }
    char *err = nullptr;
    bool ok = litert_bridge_kotlin_write_int(self->node->global_ref, raw, count, &err);
    free(raw);
    if (!ok) {
        raise_os_error_free(MP_EIO, err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_tensor_buffer_write_int_obj, litert_tensor_buffer_write_int);

mp_obj_t litert_tensor_buffer_read_int(mp_obj_t self_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);

    int32_t *out = nullptr;
    size_t len = 0;
    char *err = nullptr;
    if (!litert_bridge_kotlin_read_int(self->node->global_ref, &out, &len, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    mp_obj_t *items = (mp_obj_t *) malloc(sizeof(mp_obj_t) * len);
    for (size_t i = 0; i < len; i++) {
        items[i] = mp_obj_new_int(out[i]);
    }
    mp_obj_t result = mp_obj_new_tuple(len, items);
    free(items);
    free(out);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_tensor_buffer_read_int_obj, litert_tensor_buffer_read_int);

mp_obj_t litert_tensor_buffer_write_bool(mp_obj_t self_in, mp_obj_t data_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);

    size_t count;
    mp_obj_t *items;
    mp_obj_get_array(data_in, &count, &items);
    auto *raw = (bool *) malloc(sizeof(bool) * count);
    for (size_t i = 0; i < count; i++) {
        raw[i] = mp_obj_is_true(items[i]);
    }
    char *err = nullptr;
    bool ok = litert_bridge_kotlin_write_bool(self->node->global_ref, raw, count, &err);
    free(raw);
    if (!ok) {
        raise_os_error_free(MP_EIO, err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_tensor_buffer_write_bool_obj, litert_tensor_buffer_write_bool);

mp_obj_t litert_tensor_buffer_read_bool(mp_obj_t self_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);

    bool *out = nullptr;
    size_t len = 0;
    char *err = nullptr;
    if (!litert_bridge_kotlin_read_bool(self->node->global_ref, &out, &len, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    mp_obj_t *items = (mp_obj_t *) malloc(sizeof(mp_obj_t) * len);
    for (size_t i = 0; i < len; i++) {
        items[i] = out[i] ? mp_const_true : mp_const_false;
    }
    mp_obj_t result = mp_obj_new_tuple(len, items);
    free(items);
    free(out);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_tensor_buffer_read_bool_obj, litert_tensor_buffer_read_bool);

mp_obj_t litert_tensor_buffer_write_long(mp_obj_t self_in, mp_obj_t data_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);

    size_t count;
    mp_obj_t *items;
    mp_obj_get_array(data_in, &count, &items);
    auto *raw = (int64_t *) malloc(sizeof(int64_t) * count);
    for (size_t i = 0; i < count; i++) {
        raw[i] = (int64_t) mp_obj_get_int(items[i]);
    }
    char *err = nullptr;
    bool ok = litert_bridge_kotlin_write_long(self->node->global_ref, raw, count, &err);
    free(raw);
    if (!ok) {
        raise_os_error_free(MP_EIO, err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(litert_tensor_buffer_write_long_obj, litert_tensor_buffer_write_long);

mp_obj_t litert_tensor_buffer_read_long(mp_obj_t self_in) {
    auto *self = (litert_tensor_buffer_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_buffer_closed(self);

    int64_t *out = nullptr;
    size_t len = 0;
    char *err = nullptr;
    if (!litert_bridge_kotlin_read_long(self->node->global_ref, &out, &len, &err)) {
        raise_os_error_free(MP_EIO, err);
    }
    mp_obj_t *items = (mp_obj_t *) malloc(sizeof(mp_obj_t) * len);
    for (size_t i = 0; i < len; i++) {
        items[i] = mp_obj_new_int_from_ll(out[i]);
    }
    mp_obj_t result = mp_obj_new_tuple(len, items);
    free(items);
    free(out);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(litert_tensor_buffer_read_long_obj, litert_tensor_buffer_read_long);

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
    {MP_ROM_QSTR(MP_QSTR_write_float), MP_ROM_PTR(&litert_tensor_buffer_write_float_obj)},
    {MP_ROM_QSTR(MP_QSTR_read_float), MP_ROM_PTR(&litert_tensor_buffer_read_float_obj)},
    {MP_ROM_QSTR(MP_QSTR_write_int), MP_ROM_PTR(&litert_tensor_buffer_write_int_obj)},
    {MP_ROM_QSTR(MP_QSTR_read_int), MP_ROM_PTR(&litert_tensor_buffer_read_int_obj)},
    {MP_ROM_QSTR(MP_QSTR_write_bool), MP_ROM_PTR(&litert_tensor_buffer_write_bool_obj)},
    {MP_ROM_QSTR(MP_QSTR_read_bool), MP_ROM_PTR(&litert_tensor_buffer_read_bool_obj)},
    {MP_ROM_QSTR(MP_QSTR_write_long), MP_ROM_PTR(&litert_tensor_buffer_write_long_obj)},
    {MP_ROM_QSTR(MP_QSTR_read_long), MP_ROM_PTR(&litert_tensor_buffer_read_long_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&litert_tensor_buffer_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&litert_tensor_buffer_del_obj)},
};
MP_DEFINE_CONST_DICT(litert_tensor_buffer_locals_dict, litert_tensor_buffer_locals_dict_table);

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

const mp_rom_map_elem_t litert_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_litert)},
    {MP_ROM_QSTR(MP_QSTR_Environment), MP_ROM_PTR(&litert_environment_type)},
    {MP_ROM_QSTR(MP_QSTR_CompiledModel), MP_ROM_PTR(&litert_compiled_model_type)},
    {MP_ROM_QSTR(MP_QSTR_TensorBuffer), MP_ROM_PTR(&litert_tensor_buffer_type)},
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

extern MP_DEFINE_CONST_OBJ_TYPE(
    litert_tensor_buffer_type,
    MP_QSTR_TensorBuffer,
    MP_TYPE_FLAG_NONE,
    locals_dict, &litert_tensor_buffer_locals_dict
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
