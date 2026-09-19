// upy-android native tf module. OUR OWN code, NOT vendored OpenMV source.
// android.tf.Model wraps LiteRT's classic TfLiteInterpreter C API.
// see session-state: tf_module.cpp#module_design

#include <cmath>
#include <cstring>
#include <limits>

#include <android/NeuralNetworks.h>

#include "tflite/c/c_api.h"
#include "tflite/c/c_api_experimental.h"

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"
// ulab's ndarray
#include "ndarray.h"
}

#include "tf_module.h"

// Forward declaration needed for tf_model_make_new()
extern const mp_obj_type_t tf_model_type;

namespace {

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// Native-only registry node, deliberately never holds an mp_obj_t pointer.
// see session-state: tf_module.cpp#registry_design
struct TfModelRegistryNode {
    TfLiteInterpreter *interpreter;
    TfLiteModel *model;
    TfModelRegistryNode *next;
};

TfModelRegistryNode *g_tf_models = nullptr;

TfModelRegistryNode *registry_add(TfLiteInterpreter *interpreter, TfLiteModel *model) {
    TfModelRegistryNode *node = (TfModelRegistryNode *) malloc(sizeof(TfModelRegistryNode));
    node->interpreter = interpreter;
    node->model = model;
    node->next = g_tf_models;
    g_tf_models = node;
    return node;
}

void registry_remove(TfModelRegistryNode *node) {
    // No-op if node is null.
    if (!node) {
        return;
    }
    TfModelRegistryNode **link = &g_tf_models;
    while (*link) {
        if (*link == node) {
            *link = node->next;
            free(node);
            return;
        }
        link = &(*link)->next;
    }
}

// see session-state: tf_module.cpp#tf_model_obj_t
struct tf_model_obj_t {
    mp_obj_base_t base;
    TfLiteInterpreter *interpreter;
    TfLiteModel *model;
    // Owned; null once closed (idempotency marker).
    TfModelRegistryNode *node;
    bool input_aliased;
    mp_obj_t aliased_input_ref;
};

// Shared by close() and __del__. Idempotent.
void tf_model_close_impl(tf_model_obj_t *self) {
    if (!self->node) {
        return;
    }
    registry_remove(self->node);
    self->node = nullptr;
    TfLiteInterpreterDelete(self->interpreter);
    // Model must outlive the interpreter (TfLiteInterpreterCreate's own doc comment). Deleted here, after, not before.
    TfLiteModelDelete(self->model);
    self->interpreter = nullptr;
    self->model = nullptr;
    self->input_aliased = false;
    self->aliased_input_ref = mp_const_none;
}

mp_obj_t tf_model_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_path };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_path, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    const char *path = mp_obj_str_get_str(parsed[ARG_path].u_obj);
    // TfLiteModelCreateFromFile is not VFS-aware
    // strip the leading '/' so a VFS-absolute path resolves correctly.
    // see session-state: tf_module.cpp#tf_model_make_new
    if (path[0] == '/') {
        path++;
    }

    TfLiteModel *model = TfLiteModelCreateFromFile(path);
    if (!model) {
        raise_os_error(MP_EINVAL, "android.tf: failed to load model");
    }

    TfLiteInterpreterOptions *options = TfLiteInterpreterOptionsCreate();
    if (!options) {
        TfLiteModelDelete(model);
        raise_os_error(MP_ENOMEM, "android.tf: failed to create interpreter options");
    }
    // Best-effort NNAPI, silent CPU/XNNPACK fallback.
    // see session-state: tf_module.cpp#module_design
    TfLiteInterpreterOptionsSetUseNNAPI(options, true);
    TfLiteInterpreterOptionsSetEnableDelegateFallback(options, true);

    TfLiteInterpreter *interpreter = TfLiteInterpreterCreate(model, options);
    // Safe to delete options immediately. The interpreter doesn't retain
    // them (c_api.h's own doc comment).
    TfLiteInterpreterOptionsDelete(options);
    if (!interpreter) {
        TfLiteModelDelete(model);
        raise_os_error(MP_EINVAL, "android.tf: failed to create interpreter");
    }

    if (TfLiteInterpreterAllocateTensors(interpreter) != kTfLiteOk) {
        TfLiteInterpreterDelete(interpreter);
        TfLiteModelDelete(model);
        raise_os_error(MP_EINVAL, "android.tf: failed to allocate tensors");
    }

    tf_model_obj_t *self = mp_obj_malloc_with_finaliser(tf_model_obj_t, &tf_model_type);
    self->interpreter = interpreter;
    self->model = model;
    self->node = registry_add(interpreter, model);
    self->input_aliased = false;
    self->aliased_input_ref = mp_const_none;
    return MP_OBJ_FROM_PTR(self);
}

void raise_if_closed(tf_model_obj_t *self) {
    if (!self->node) {
        raise_os_error(MP_EINVAL, "android.tf: model closed -- call Model() again");
    }
}

// see session-state: tf_module.cpp#tf_model_set_input
mp_obj_t tf_model_set_input(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    tf_model_obj_t *self = (tf_model_obj_t *) MP_OBJ_TO_PTR(pos_args[0]);
    raise_if_closed(self);

    enum { ARG_data, ARG_index };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_data, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_index, MP_ARG_INT, {.u_int = 0}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    mp_int_t index = args[ARG_index].u_int;

    TfLiteTensor *tensor = TfLiteInterpreterGetInputTensor(self->interpreter, index);
    if (!tensor) {
        raise_os_error(MP_EINVAL, "android.tf: no input tensor at that index");
    }

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_data].u_obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len != TfLiteTensorByteSize(tensor)) {
        raise_os_error(MP_EINVAL, "android.tf: input data size does not match the tensor's byte size");
    }

    bool aligned = index == 0 && (((uintptr_t) bufinfo.buf) % 64) == 0;

    if (self->input_aliased && !aligned) {
        raise_os_error(MP_EINVAL,
            "android.tf: model is in zero-copy mode -- once aliased, every "
            "set_input() call must provide a 64-byte-aligned buffer at index 0; "
            "construct a new Model() to go back to copying");
    }

    if (!self->input_aliased && !aligned) {
        if (TfLiteTensorCopyFromBuffer(tensor, bufinfo.buf, bufinfo.len) != kTfLiteOk) {
            raise_os_error(MP_EIO, "android.tf: failed to copy input data into tensor");
        }
        return mp_const_none;
    }

    // GC reference stored BEFORE the call, not after.
    self->aliased_input_ref = args[ARG_data].u_obj;

    int32_t tensor_index = TfLiteInterpreterGetInputTensorIndex(self->interpreter, index);
    TfLiteCustomAllocation allocation = {(void *) bufinfo.buf, bufinfo.len};
    if (TfLiteInterpreterSetCustomAllocationForTensor(self->interpreter, tensor_index, &allocation, kTfLiteCustomAllocationFlagsNone) != kTfLiteOk) {
        // Tensor untouched on failure. Safe to fall back to a plain copy.
        self->aliased_input_ref = mp_const_none;
        if (TfLiteTensorCopyFromBuffer(tensor, bufinfo.buf, bufinfo.len) != kTfLiteOk) {
            raise_os_error(MP_EIO, "android.tf: failed to copy input data into tensor");
        }
        return mp_const_none;
    }

    // Must re-run AllocateTensors() after SetCustomAllocationForTensor
    // (c_api_experimental.h's own documented requirement).
    self->input_aliased = true;
    if (TfLiteInterpreterAllocateTensors(self->interpreter) != kTfLiteOk) {
        raise_os_error(MP_EIO,
            "android.tf: zero-copy alias succeeded but reallocating tensors "
            "afterward failed -- this model is no longer usable, construct a "
            "new Model()");
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(tf_model_set_input_obj, 1, tf_model_set_input);

mp_obj_t tf_model_invoke(mp_obj_t self_in) {
    tf_model_obj_t *self = (tf_model_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);
    if (TfLiteInterpreterInvoke(self->interpreter) != kTfLiteOk) {
        raise_os_error(MP_EIO, "android.tf: invoke failed");
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(tf_model_invoke_obj, tf_model_invoke);

// Real copy (mp_obj_new_bytes()), never a view into the tensor arena.
// That arena is reused by the next invoke() and freed on close(); a
// zero-copy view here would be a live footgun.
mp_obj_t tf_model_get_output(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    tf_model_obj_t *self = (tf_model_obj_t *) MP_OBJ_TO_PTR(pos_args[0]);
    raise_if_closed(self);

    enum { ARG_index };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_index, MP_ARG_INT, {.u_int = 0}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const TfLiteTensor *tensor = TfLiteInterpreterGetOutputTensor(self->interpreter, args[ARG_index].u_int);
    if (!tensor) {
        raise_os_error(MP_EINVAL, "android.tf: no output tensor at that index");
    }
    return mp_obj_new_bytes((const byte *) TfLiteTensorData(tensor), TfLiteTensorByteSize(tensor));
}
static MP_DEFINE_CONST_FUN_OBJ_KW(tf_model_get_output_obj, 1, tf_model_get_output);

// TfLiteType -> ulab NDARRAY_* dtype char. ulab only has four numeric
// dtypes (UINT8/INT8/UINT16/INT16/FLOAT)
// -1 signals unsupported.
int tf_ulab_dtype_for_tflite(TfLiteType type) {
    switch (type) {
        case kTfLiteFloat32: return NDARRAY_FLOAT;
        case kTfLiteInt8: return NDARRAY_INT8;
        case kTfLiteUInt8: return NDARRAY_UINT8;
        case kTfLiteInt16: return NDARRAY_INT16;
        case kTfLiteUInt16: return NDARRAY_UINT16;
        default: return -1;
    }
}

// round to nearest, then saturate to T's range.
// see session-state: tf_module.cpp#tf_quantize_round_clamp
template <typename T>
T tf_quantize_round_clamp(float raw) {
    float rounded = roundf(raw);
    float lo = (float) std::numeric_limits<T>::min();
    float hi = (float) std::numeric_limits<T>::max();
    return (T) fmaxf(lo, fminf(hi, rounded));
}

// ulab-ndarray-typed sibling of set_input(), a separate method (not a
// type-check inside set_input()) so set_input()'s own invariants stay
// untouched.
// see session-state: tf_module.cpp#tf_model_set_input_ndarray
mp_obj_t tf_model_set_input_ndarray(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    tf_model_obj_t *self = (tf_model_obj_t *) MP_OBJ_TO_PTR(pos_args[0]);
    raise_if_closed(self);

    enum { ARG_data, ARG_index };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_data, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_index, MP_ARG_INT, {.u_int = 0}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    mp_int_t index = args[ARG_index].u_int;

    if (!mp_obj_is_type(args[ARG_data].u_obj, &ulab_ndarray_type)) {
        raise_os_error(MP_EINVAL, "android.tf: set_input_ndarray() requires a ulab ndarray");
    }
    ndarray_obj_t *src = (ndarray_obj_t *) MP_OBJ_TO_PTR(args[ARG_data].u_obj);

    TfLiteTensor *tensor = TfLiteInterpreterGetInputTensor(self->interpreter, index);
    if (!tensor) {
        raise_os_error(MP_EINVAL, "android.tf: no input tensor at that index");
    }

    // Can't satisfy the zero-copy-alias monotonic invariant with a
    // converted write.
    // see session-state: tf_module.cpp#tf_model_set_input
    if (self->input_aliased && index == 0) {
        raise_os_error(MP_EINVAL,
            "android.tf: model is in zero-copy mode -- set_input_ndarray() "
            "can't alias, construct a new Model() to go back to copying");
    }

    size_t expected_len = 1;
    int32_t ndim = TfLiteTensorNumDims(tensor);
    for (int32_t i = 0; i < ndim; i++) {
        expected_len *= TfLiteTensorDim(tensor, i);
    }
    if (src->len != expected_len) {
        raise_os_error(MP_EINVAL, "android.tf: ndarray length does not match the tensor's element count");
    }

    TfLiteType tensor_type = TfLiteTensorType(tensor);
    void *dst = TfLiteTensorData(tensor);
    size_t len = src->len;

    if (tensor_type == kTfLiteFloat32) {
        float *dst_f = (float *) dst;
        for (size_t i = 0; i < len; i++) {
            dst_f[i] = (float) ndarray_get_float_index(src->array, src->dtype, i);
        }
        return mp_const_none;
    }

    if (tensor_type == kTfLiteInt8 || tensor_type == kTfLiteUInt8 ||
        tensor_type == kTfLiteInt16 || tensor_type == kTfLiteUInt16) {
        // real = scale * (raw - zero_point), inverted: raw = value/scale + zero_point.
        TfLiteQuantizationParams quant = TfLiteTensorQuantizationParams(tensor);
        if (quant.scale == 0.0f) {
            raise_os_error(MP_EINVAL,
                "android.tf: set_input_ndarray() requires per-tensor scale/zero_point; "
                "this tensor has none (possibly per-channel quantized), use set_input() instead");
        }
        float inv_scale = 1.0f / quant.scale;
        for (size_t i = 0; i < len; i++) {
            float v = (float) ndarray_get_float_index(src->array, src->dtype, i);
            float raw = v * inv_scale + quant.zero_point;
            switch (tensor_type) {
                case kTfLiteInt8: ((int8_t *) dst)[i] = tf_quantize_round_clamp<int8_t>(raw); break;
                case kTfLiteUInt8: ((uint8_t *) dst)[i] = tf_quantize_round_clamp<uint8_t>(raw); break;
                case kTfLiteInt16: ((int16_t *) dst)[i] = tf_quantize_round_clamp<int16_t>(raw); break;
                case kTfLiteUInt16: ((uint16_t *) dst)[i] = tf_quantize_round_clamp<uint16_t>(raw); break;
                default: break;
            }
        }
        return mp_const_none;
    }

    raise_os_error(MP_EINVAL,
        "android.tf: set_input_ndarray() only supports float32/int8/uint8/int16/uint16 tensors");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(tf_model_set_input_ndarray_obj, 1, tf_model_set_input_ndarray);

// ulab-ndarray-typed sibling of get_output()
// quantized tensors auto-dequantize to a float ndarray (OpenMV-style).
// The float32 path below is a real memcpy, not a convert loop,
// and is COUPLED to MICROPY_FLOAT_IMPL being FLOAT
// see session-state: tf_module.cpp#tf_model_get_output_ndarray before touching either.
mp_obj_t tf_model_get_output_ndarray(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    tf_model_obj_t *self = (tf_model_obj_t *) MP_OBJ_TO_PTR(pos_args[0]);
    raise_if_closed(self);

    enum { ARG_index };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_index, MP_ARG_INT, {.u_int = 0}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const TfLiteTensor *tensor = TfLiteInterpreterGetOutputTensor(self->interpreter, args[ARG_index].u_int);
    if (!tensor) {
        raise_os_error(MP_EINVAL, "android.tf: no output tensor at that index");
    }

    TfLiteType tensor_type = TfLiteTensorType(tensor);
    int ulab_dtype = tf_ulab_dtype_for_tflite(tensor_type);
    if (ulab_dtype < 0) {
        raise_os_error(MP_EINVAL,
            "android.tf: get_output_ndarray() only supports float32/int8/uint8/int16/uint16 tensors");
    }

    int32_t ndim = TfLiteTensorNumDims(tensor);
    if (ndim > ULAB_MAX_DIMS) {
        raise_os_error(MP_EINVAL, "android.tf: tensor has more dimensions than ulab supports");
    }
    // ulab shape arrays are right-aligned within a fixed ULAB_MAX_DIMS slots
    // see ndarray_new_ndarray()'s own internal convention.
    size_t shape[ULAB_MAX_DIMS] = {};
    for (int32_t i = 0; i < ndim; i++) {
        shape[ULAB_MAX_DIMS - ndim + i] = (size_t) TfLiteTensorDim(tensor, i);
    }

    const void *src = TfLiteTensorData(tensor);

    if (tensor_type == kTfLiteFloat32) {
        ndarray_obj_t *out = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
        memcpy(out->array, src, out->len * sizeof(float));
        return MP_OBJ_FROM_PTR(out);
    }

    TfLiteQuantizationParams quant = TfLiteTensorQuantizationParams(tensor);
    if (quant.scale == 0.0f) {
        raise_os_error(MP_EINVAL,
            "android.tf: get_output_ndarray() requires per-tensor scale/zero_point; "
            "this tensor has none (possibly per-channel quantized), use get_output() instead");
    }
    ndarray_obj_t *out = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
    float *dst_f = (float *) out->array;
    for (size_t i = 0; i < out->len; i++) {
        float raw;
        switch (tensor_type) {
            case kTfLiteInt8: raw = ((const int8_t *) src)[i]; break;
            case kTfLiteUInt8: raw = ((const uint8_t *) src)[i]; break;
            case kTfLiteInt16: raw = ((const int16_t *) src)[i]; break;
            case kTfLiteUInt16: raw = ((const uint16_t *) src)[i]; break;
            default: raw = 0; break;
        }
        dst_f[i] = (raw - quant.zero_point) * quant.scale;
    }
    return MP_OBJ_FROM_PTR(out);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(tf_model_get_output_ndarray_obj, 1, tf_model_get_output_ndarray);

// TfLiteType -> canonical dtype name string
// numpy/ML-ecosystem naming, not an invented label.
// Pulled from tflite_types.h's 24-value enum directly;
// anything outside that set falls back to "unknown(<int>)" rather than raising.
// info() is a diagnostic call.
const char *tf_dtype_name(TfLiteType type, char *fallback_buf, size_t fallback_buf_size) {
    switch (type) {
        case kTfLiteNoType: return "no_type";
        case kTfLiteFloat32: return "float32";
        case kTfLiteInt32: return "int32";
        case kTfLiteUInt8: return "uint8";
        case kTfLiteInt64: return "int64";
        case kTfLiteString: return "string";
        case kTfLiteBool: return "bool";
        case kTfLiteInt16: return "int16";
        case kTfLiteComplex64: return "complex64";
        case kTfLiteInt8: return "int8";
        case kTfLiteFloat16: return "float16";
        case kTfLiteFloat64: return "float64";
        case kTfLiteComplex128: return "complex128";
        case kTfLiteUInt64: return "uint64";
        case kTfLiteResource: return "resource";
        case kTfLiteVariant: return "variant";
        case kTfLiteUInt32: return "uint32";
        case kTfLiteUInt16: return "uint16";
        case kTfLiteInt4: return "int4";
        case kTfLiteBFloat16: return "bfloat16";
        case kTfLiteInt2: return "int2";
        case kTfLiteUInt4: return "uint4";
        case kTfLiteFloat8E4M3FN: return "float8e4m3fn";
        case kTfLiteFloat8E5M2: return "float8e5m2";
        default:
            snprintf(fallback_buf, fallback_buf_size, "unknown(%d)", (int) type);
            return fallback_buf;
    }
}

// One dict per tensor: {'name', 'shape', 'dtype', 'bytes', 'scale', 'zero_point'}.
// scale/zero_point are load-bearing, not decorative.
// see session-state: tf_module.cpp#tf_tensor_info_dict before removing scale/zero_point.
mp_obj_t tf_tensor_info_dict(const TfLiteTensor *tensor) {
    mp_obj_t dict = mp_obj_new_dict(6);

    const char *name = TfLiteTensorName(tensor);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_name), mp_obj_new_str(name, strlen(name)));

    mp_obj_t shape = mp_obj_new_list(0, nullptr);
    int32_t num_dims = TfLiteTensorNumDims(tensor);
    for (int32_t i = 0; i < num_dims; i++) {
        mp_obj_list_append(shape, mp_obj_new_int(TfLiteTensorDim(tensor, i)));
    }
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_shape), shape);

    char fallback_buf[24];
    const char *dtype = tf_dtype_name(TfLiteTensorType(tensor), fallback_buf, sizeof(fallback_buf));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_dtype), mp_obj_new_str(dtype, strlen(dtype)));

    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_bytes), mp_obj_new_int_from_uint(TfLiteTensorByteSize(tensor)));

    TfLiteQuantizationParams quant = TfLiteTensorQuantizationParams(tensor);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_scale), mp_obj_new_float(quant.scale));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_zero_point), mp_obj_new_int(quant.zero_point));

    return dict;
}

// input_aliased reports whether the LAST set_input() call took the zero-copy alias path.
mp_obj_t tf_model_info(mp_obj_t self_in) {
    tf_model_obj_t *self = (tf_model_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);

    mp_obj_t inputs = mp_obj_new_list(0, nullptr);
    int32_t n_in = TfLiteInterpreterGetInputTensorCount(self->interpreter);
    for (int32_t i = 0; i < n_in; i++) {
        mp_obj_list_append(inputs, tf_tensor_info_dict(TfLiteInterpreterGetInputTensor(self->interpreter, i)));
    }

    mp_obj_t outputs = mp_obj_new_list(0, nullptr);
    int32_t n_out = TfLiteInterpreterGetOutputTensorCount(self->interpreter);
    for (int32_t i = 0; i < n_out; i++) {
        mp_obj_list_append(outputs, tf_tensor_info_dict(TfLiteInterpreterGetOutputTensor(self->interpreter, i)));
    }

    mp_obj_t dict = mp_obj_new_dict(3);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_inputs), inputs);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_outputs), outputs);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_input_aliased), self->input_aliased ? mp_const_true : mp_const_false);
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_1(tf_model_info_obj, tf_model_info);

mp_obj_t tf_model_close(mp_obj_t self_in) {
    tf_model_close_impl((tf_model_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(tf_model_close_obj, tf_model_close);

// __del__. Only reached by py/gc.c's finaliser sweep, never called
// directly by a script.
mp_obj_t tf_model_del(mp_obj_t self_in) {
    tf_model_close_impl((tf_model_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(tf_model_del_obj, tf_model_del);

const mp_rom_map_elem_t tf_model_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_set_input), MP_ROM_PTR(&tf_model_set_input_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_input_ndarray), MP_ROM_PTR(&tf_model_set_input_ndarray_obj)},
    {MP_ROM_QSTR(MP_QSTR_invoke), MP_ROM_PTR(&tf_model_invoke_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_output), MP_ROM_PTR(&tf_model_get_output_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_output_ndarray), MP_ROM_PTR(&tf_model_get_output_ndarray_obj)},
    {MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&tf_model_info_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&tf_model_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&tf_model_del_obj)},
};
MP_DEFINE_CONST_DICT(tf_model_locals_dict, tf_model_locals_dict_table);

// android.tf.info()'s hw_nnapi field.
// see session-state: tf_module.cpp#tf_hw_nnapi_available before touching the availability guard below.
// This reproduces a real build error this project already hit once if done wrong.
bool tf_hw_nnapi_available() {
    if (__builtin_available(android 29, *)) {
        uint32_t count = 0;
        if (ANeuralNetworks_getDeviceCount(&count) != ANEURALNETWORKS_NO_ERROR) {
            return false;
        }

        for (uint32_t i = 0; i < count; i++) {
            ANeuralNetworksDevice *device = nullptr;
            if (ANeuralNetworks_getDevice(i, &device) != ANEURALNETWORKS_NO_ERROR) {
                continue;
            }
            int32_t type = ANEURALNETWORKS_DEVICE_UNKNOWN;
            if (ANeuralNetworksDevice_getType(device, &type) == ANEURALNETWORKS_NO_ERROR &&
                (type == ANEURALNETWORKS_DEVICE_GPU || type == ANEURALNETWORKS_DEVICE_ACCELERATOR)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

// extern prefix required
// MP_DEFINE_CONST_OBJ_TYPE has internal linkage by default in C++ even at file scope
// (same rule already hit for MP_DEFINE_CONST_FUN_OBJ_N objects in imu_module.cpp/camera_module.cpp).
extern MP_DEFINE_CONST_OBJ_TYPE(
    tf_model_type,
    MP_QSTR_Model,
    MP_TYPE_FLAG_NONE,
    make_new, tf_model_make_new,
    locals_dict, &tf_model_locals_dict
    );

extern "C" void tf_close_all(void) {
    while (g_tf_models) {
        TfModelRegistryNode *node = g_tf_models;
        g_tf_models = node->next;
        TfLiteInterpreterDelete(node->interpreter);
        TfLiteModelDelete(node->model);
        free(node);
    }
}

// android.tf.info() returns {'version': str, 'hw_nnapi': bool}.
// Declared outside the anonymous namespace so android_module.cpp can reference the function object.
// (extern prefix required, same linkage reasoning as tf_model_type above)
mp_obj_t android_tf_info() {
    mp_obj_t dict = mp_obj_new_dict(2);
    const char *version = TfLiteVersion();
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_version), mp_obj_new_str(version, strlen(version)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_hw_nnapi), tf_hw_nnapi_available() ? mp_const_true : mp_const_false);
    return dict;
}
extern MP_DEFINE_CONST_FUN_OBJ_0(android_tf_info_obj, android_tf_info);
