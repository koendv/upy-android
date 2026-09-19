// upy-android native rt module. OUR OWN code, NOT vendored OpenMV source.
// android.rt.Model wraps LiteRT's newer LiteRtEnvironment/
// LiteRtCompiledModel C API. android.tf.Model (tf_module.cpp) wraps the
// older, separate TfLiteInterpreter API. Both link the same libLiteRt.so.
// see session-state: rt_module.cpp#module_design

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "litert/c/internal/litert_accelerator.h"
#include "litert/c/litert_common.h"
#include "litert/c/litert_compiled_model.h"
#include "litert/c/litert_environment.h"
#include "litert/c/litert_layout.h"
#include "litert/c/litert_model.h"
#include "litert/c/litert_model_types.h"
#include "litert/c/litert_options.h"
#include "litert/c/litert_tensor_buffer.h"
#include "litert/c/litert_tensor_buffer_requirements.h"
#include "litert/c/litert_tensor_buffer_types.h"

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"
// ulab's ndarray
#include "ndarray.h"
}

#include "rt_module.h"

// Forward declaration needed for rt_model_make_new()
extern const mp_obj_type_t rt_model_type;

namespace {

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// LiteRtStatus has no message-string accessor (unlike TfLiteStatus's
// implicit pairing with TfLiteInterpreter's own richer error reporting).
// The numeric code is the only diagnostic this module can surface, but
// that is still far better than a bare "failed."
void raise_os_error_status(int errno_, const char *msg, LiteRtStatus status) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s (LiteRtStatus=%d)", msg, (int) status);
    raise_os_error(errno_, buf);
}

// Native-only registry node, deliberately never holds an mp_obj_t pointer.
// Same reasoning as tf_module.cpp#registry_design. g_rt_environment is
// separate: it is the shared LiteRtEnvironment every Model's
// LiteRtCompiledModel was created from, not a per-Model resource, so it
// is not itself part of this registry. See rt_close_all()'s own
// ordering.
// see session-state: rt_module.cpp#registry_design
struct RtModelRegistryNode {
    LiteRtCompiledModel compiled_model;
    LiteRtModel model;
    RtModelRegistryNode *next;
};

RtModelRegistryNode *g_rt_models = nullptr;
LiteRtEnvironment g_rt_environment = nullptr;

RtModelRegistryNode *registry_add(LiteRtCompiledModel compiled_model, LiteRtModel model) {
    RtModelRegistryNode *node = (RtModelRegistryNode *) malloc(sizeof(RtModelRegistryNode));
    node->compiled_model = compiled_model;
    node->model = model;
    node->next = g_rt_models;
    g_rt_models = node;
    return node;
}

void registry_remove(RtModelRegistryNode *node) {
    // No-op if node is null.
    if (!node) {
        return;
    }
    RtModelRegistryNode **link = &g_rt_models;
    while (*link) {
        if (*link == node) {
            *link = node->next;
            free(node);
            return;
        }
        link = &(*link)->next;
    }
}

// Cached per-tensor facts, one entry per input/output index, filled once
// at Model construction (static-shape assumption, same as tf_module.cpp).
//
// ranked_type.layout.dimensions can legitimately contain a dynamic dim
// (-1) even for a tensor the COMPILED model has already resolved to one
// concrete size. Real, hit on real hardware, not a hypothetical: this
// fixture's signature declares shape [-1, 32, 32] (a nominal dynamic
// batch dim from the original export), while
// LiteRtGetCompiledModelInputBufferRequirements still resolves a
// concrete 1024-byte buffer for it. The classic TfLiteInterpreter API
// never exposes this distinction (it only ever returns the allocated/
// resolved shape), so android.tf never hit it. rt_fill_tensor_info()
// overwrites any dynamic dim in ranked_type.layout.dimensions with its
// resolved value (verified against buffer_size, not guessed) so this
// cached copy always carries a fully concrete shape, matching what
// LiteRtCreateTensorBufferFromHostMemory itself requires.
// see session-state: rt_module.cpp#tensor_info_caching
struct RtTensorInfo {
    LiteRtRankedTensorType ranked_type;
    size_t buffer_size;
    LiteRtQuantizationTypeId quant_type_id;
    LiteRtQuantizationPerTensor quant;
    size_t num_elements;
};

// see session-state: rt_module.cpp#rt_model_obj_t
struct rt_model_obj_t {
    mp_obj_base_t base;
    LiteRtModel model;
    LiteRtCompiledModel compiled_model;
    LiteRtSignature signature;
    // Owned; null once closed (idempotency marker).
    RtModelRegistryNode *node;
    LiteRtParamIndex num_inputs;
    LiteRtParamIndex num_outputs;
    RtTensorInfo *input_info;
    RtTensorInfo *output_info;
    // nullptr until set_input_ndarray()/invoke() populates each slot.
    LiteRtTensorBuffer *input_bufs;
    LiteRtTensorBuffer *output_bufs;
};

// Destroys the tensor buffer in *slot if set, freeing its host backing
// memory first (deallocator was NULL at creation time; this module
// owns that memory, LiteRt does not). Idempotent: no-op if *slot is null.
void rt_destroy_buffer_slot(LiteRtTensorBuffer *slot) {
    if (!*slot) {
        return;
    }
    void *host_mem = nullptr;
    if (LiteRtGetTensorBufferHostMemory(*slot, &host_mem) == kLiteRtStatusOk) {
        free(host_mem);
    }
    LiteRtDestroyTensorBuffer(*slot);
    *slot = nullptr;
}

// Shared by close() and __del__. Idempotent.
void rt_model_close_impl(rt_model_obj_t *self) {
    if (!self->node) {
        return;
    }
    for (LiteRtParamIndex i = 0; i < self->num_inputs; i++) {
        rt_destroy_buffer_slot(&self->input_bufs[i]);
    }
    for (LiteRtParamIndex i = 0; i < self->num_outputs; i++) {
        rt_destroy_buffer_slot(&self->output_bufs[i]);
    }
    free(self->input_bufs);
    free(self->output_bufs);
    free(self->input_info);
    free(self->output_info);
    registry_remove(self->node);
    self->node = nullptr;
    LiteRtDestroyCompiledModel(self->compiled_model);
    // Model must outlive the compiled model (mirrors TfLiteModel/
    // TfLiteInterpreter's own ordering in tf_module.cpp). Deleted here,
    // after, not before.
    LiteRtDestroyModel(self->model);
    self->compiled_model = nullptr;
    self->model = nullptr;
    self->input_bufs = nullptr;
    self->output_bufs = nullptr;
    self->input_info = nullptr;
    self->output_info = nullptr;
}

// LiteRtElementType -> ulab NDARRAY_* dtype char. Same four numeric
// dtypes ulab supports as tf_ulab_dtype_for_tflite() in tf_module.cpp.
// -1 signals unsupported.
int rt_ulab_dtype_for_litert(LiteRtElementType type) {
    switch (type) {
        case kLiteRtElementTypeFloat32: return NDARRAY_FLOAT;
        case kLiteRtElementTypeInt8: return NDARRAY_INT8;
        case kLiteRtElementTypeUInt8: return NDARRAY_UINT8;
        case kLiteRtElementTypeInt16: return NDARRAY_INT16;
        case kLiteRtElementTypeUInt16: return NDARRAY_UINT16;
        default: return -1;
    }
}

// Byte size of one element, for the same four numeric dtypes plus
// float32. -1 signals unsupported (mirrors rt_ulab_dtype_for_litert()'s
// own set, checked together in rt_fill_tensor_info()).
int rt_element_byte_size(LiteRtElementType type) {
    switch (type) {
        case kLiteRtElementTypeFloat32: return 4;
        case kLiteRtElementTypeInt8: return 1;
        case kLiteRtElementTypeUInt8: return 1;
        case kLiteRtElementTypeInt16: return 2;
        case kLiteRtElementTypeUInt16: return 2;
        default: return -1;
    }
}

// round to nearest, then saturate to T's range. Same algorithm as
// tf_module.cpp's tf_quantize_round_clamp, ported for LiteRtTensorBuffer
// instead of TfLiteTensor. See that function's own annotation entry
// for the round/clamp bug this pattern fixes.
// see session-state: tf_module.cpp#tf_quantize_round_clamp
template <typename T>
T rt_quantize_round_clamp(float raw) {
    float rounded = roundf(raw);
    float lo = (float) std::numeric_limits<T>::min();
    float hi = (float) std::numeric_limits<T>::max();
    return (T) fmaxf(lo, fminf(hi, rounded));
}

// Fills one RtTensorInfo from a real model tensor, raising clearly for
// anything this module does not support: unranked tensors, per-channel/
// block-wise quantization, or a non-numeric element type.
// see session-state: rt_module.cpp#tensor_info_caching
void rt_fill_tensor_info(LiteRtCompiledModel compiled_model, bool is_output,
                          LiteRtSignature signature, LiteRtParamIndex index,
                          RtTensorInfo *info) {
    LiteRtTensor tensor;
    LiteRtStatus status = is_output
        ? LiteRtGetSignatureOutputTensorByIndex(signature, index, &tensor)
        : LiteRtGetSignatureInputTensorByIndex(signature, index, &tensor);
    if (status != kLiteRtStatusOk) {
        raise_os_error(MP_EINVAL, "android.rt: no tensor at that index");
    }

    LiteRtTensorTypeId type_id;
    if (LiteRtGetTensorTypeId(tensor, &type_id) != kLiteRtStatusOk ||
        type_id != kLiteRtRankedTensorType) {
        raise_os_error(MP_EINVAL, "android.rt: only ranked (static-shape) tensors are supported");
    }
    if (LiteRtGetRankedTensorType(tensor, &info->ranked_type) != kLiteRtStatusOk) {
        raise_os_error(MP_EINVAL, "android.rt: failed to read tensor type");
    }
    if (rt_ulab_dtype_for_litert(info->ranked_type.element_type) < 0) {
        raise_os_error(MP_EINVAL, "android.rt: only float32/int8/uint8/int16/uint16 tensors are supported");
    }

    LiteRtTensorBufferRequirements reqs;
    LiteRtStatus reqs_status = is_output
        ? LiteRtGetCompiledModelOutputBufferRequirements(compiled_model, 0, index, &reqs)
        : LiteRtGetCompiledModelInputBufferRequirements(compiled_model, 0, index, &reqs);
    if (reqs_status != kLiteRtStatusOk) {
        raise_os_error(MP_EINVAL, "android.rt: failed to query tensor buffer requirements");
    }
    if (LiteRtGetTensorBufferRequirementsBufferSize(reqs, &info->buffer_size) != kLiteRtStatusOk) {
        raise_os_error(MP_EINVAL, "android.rt: failed to read tensor buffer size");
    }

    // Resolve any dynamic (-1) dims against the COMPILED model's own
    // buffer_size, verified rather than guessed. See this struct's
    // own comment for why this is needed at all. At most one dynamic
    // dim is resolvable this way (its value is exactly determined by
    // dividing out every already-concrete dim); more than one is
    // genuinely ambiguous and raises.
    int elem_size = rt_element_byte_size(info->ranked_type.element_type);
    if (info->buffer_size % (size_t) elem_size != 0) {
        raise_os_error(MP_EINVAL, "android.rt: tensor buffer size is not a whole number of elements");
    }
    info->num_elements = info->buffer_size / (size_t) elem_size;

    int32_t rank = info->ranked_type.layout.rank;
    size_t known_product = 1;
    int dynamic_dim_index = -1;
    for (int32_t i = 0; i < rank; i++) {
        int32_t dim = info->ranked_type.layout.dimensions[i];
        if (dim < 0) {
            if (dynamic_dim_index >= 0) {
                raise_os_error(MP_EINVAL, "android.rt: tensor has more than one dynamic dimension, not supported");
            }
            dynamic_dim_index = i;
        } else {
            known_product *= (size_t) dim;
        }
    }
    if (dynamic_dim_index >= 0) {
        if (known_product == 0 || info->num_elements % known_product != 0) {
            raise_os_error(MP_EINVAL, "android.rt: tensor shape does not match its own buffer size");
        }
        // Overwrite the cached copy's own dynamic dim with its resolved
        // value. Everything downstream (buffer creation, ulab shape,
        // info()) reads ranked_type.layout.dimensions directly and must
        // see a fully concrete shape.
        info->ranked_type.layout.dimensions[dynamic_dim_index] =
            (int32_t) (info->num_elements / known_product);
    } else if (known_product != info->num_elements) {
        raise_os_error(MP_EINVAL, "android.rt: tensor shape does not match its own buffer size");
    }

    if (LiteRtGetQuantizationTypeId(tensor, &info->quant_type_id) != kLiteRtStatusOk) {
        raise_os_error(MP_EINVAL, "android.rt: failed to read tensor quantization type");
    }
    if (info->quant_type_id == kLiteRtQuantizationPerTensor) {
        if (LiteRtGetPerTensorQuantization(tensor, &info->quant) != kLiteRtStatusOk) {
            raise_os_error(MP_EINVAL, "android.rt: failed to read per-tensor quantization");
        }
    } else if (info->quant_type_id != kLiteRtQuantizationNone) {
        // Per-channel/block-wise: raise clearly rather than silently
        // mismap, same "raise, don't guess" pattern as tf_tensor_info_dict.
        raise_os_error(MP_EINVAL,
            "android.rt: only per-tensor quantization is supported "
            "(this tensor is per-channel or block-wise quantized)");
    }
}

mp_obj_t rt_model_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_path };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_path, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    const char *path = mp_obj_str_get_str(parsed[ARG_path].u_obj);
    // LiteRtCreateModelFromFile is not VFS-aware, same trap as
    // TfLiteModelCreateFromFile.
    // see session-state: tf_module.cpp#tf_model_make_new
    if (path[0] == '/') {
        path++;
    }

    // Lazily created once, shared by every Model. Accelerator
    // auto-registration lives on the environment, not per-Model.
    // see session-state: rt_module.cpp#environment_lifecycle
    if (!g_rt_environment) {
        if (LiteRtCreateEnvironment(0, nullptr, &g_rt_environment) != kLiteRtStatusOk) {
            raise_os_error(MP_ENOMEM, "android.rt: failed to create environment");
        }
    }

    LiteRtModel model;
    LiteRtStatus status = LiteRtCreateModelFromFile(g_rt_environment, path, &model);
    if (status != kLiteRtStatusOk) {
        raise_os_error_status(MP_EINVAL, "android.rt: failed to load model", status);
    }

    // NOT actually optional, despite litert_compiled_model.h's own doc
    // comment ("options...is optional and can be null"). The real
    // implementation (compiled_model.cc) raises InvalidArgument for a
    // null options object, and separately for one with no hardware
    // accelerator set. Confirmed by reading that source directly after
    // hitting exactly this failure on real hardware, not assumed from
    // the header.
    //
    // CPU only, not CPU|GPU|NPU: a real GPU-accelerated run was tried
    // and produced silently wrong output. Confirmed on two real
    // devices, Adreno 610 and Adreno 710, same failure pattern on
    // both, not a per-device quirk and not a logic bug in this file.
    // A sentinel byte pattern written into the output host buffer
    // before invoke() was still present, unchanged, after invoke()
    // returned success, on every run, on both devices. The
    // GPU-accelerated compiled model never writes to this buffer at
    // all; this is not a synchronization/timing gap (which would read
    // as inconsistent partial data, not an untouched buffer).
    // CPU/XNNPACK execution verified correct against the same known-good
    // values, on both devices. Real GPU acceleration needs the actual
    // output path traced further before it can be enabled; deliberately
    // scoped out of v1, not forgotten.
    // see session-state: rt_module.cpp#rt_model_make_new
    LiteRtOptions options;
    status = LiteRtCreateOptions(&options);
    if (status != kLiteRtStatusOk) {
        LiteRtDestroyModel(model);
        raise_os_error_status(MP_ENOMEM, "android.rt: failed to create compilation options", status);
    }
    LiteRtSetOptionsHardwareAccelerators(options, kLiteRtHwAcceleratorCpu);

    LiteRtCompiledModel compiled_model;
    status = LiteRtCreateCompiledModel(g_rt_environment, model, options, &compiled_model);
    // Not retained by a successful Create() past its own return (
    // ScopedCompilationOptionsModifier in compiled_model.cc scopes it to
    // the call), safe to destroy here regardless of outcome.
    LiteRtDestroyOptions(options);
    if (status != kLiteRtStatusOk) {
        LiteRtDestroyModel(model);
        raise_os_error_status(MP_EINVAL, "android.rt: failed to compile model", status);
    }

    LiteRtSignature signature;
    status = LiteRtGetModelSignature(model, 0, &signature);
    if (status != kLiteRtStatusOk) {
        LiteRtDestroyCompiledModel(compiled_model);
        LiteRtDestroyModel(model);
        raise_os_error_status(MP_EINVAL, "android.rt: model has no signature at index 0", status);
    }

    LiteRtParamIndex num_inputs = 0;
    LiteRtParamIndex num_outputs = 0;
    LiteRtGetNumSignatureInputs(signature, &num_inputs);
    LiteRtGetNumSignatureOutputs(signature, &num_outputs);

    rt_model_obj_t *self = mp_obj_malloc_with_finaliser(rt_model_obj_t, &rt_model_type);
    self->model = model;
    self->compiled_model = compiled_model;
    self->signature = signature;
    self->num_inputs = num_inputs;
    self->num_outputs = num_outputs;
    self->input_info = (RtTensorInfo *) calloc(num_inputs, sizeof(RtTensorInfo));
    self->output_info = (RtTensorInfo *) calloc(num_outputs, sizeof(RtTensorInfo));
    self->input_bufs = (LiteRtTensorBuffer *) calloc(num_inputs, sizeof(LiteRtTensorBuffer));
    self->output_bufs = (LiteRtTensorBuffer *) calloc(num_outputs, sizeof(LiteRtTensorBuffer));
    self->node = registry_add(compiled_model, model);

    // Cache every tensor's shape/dtype/quantization/buffer-size once here,
    // static-shape assumption, same as tf_module.cpp. Any raise inside
    // rt_fill_tensor_info() below unwinds through nlr_raise(); self is
    // already GC-tracked (mp_obj_malloc_with_finaliser) and already
    // registered (registry_add above), so rt_model_del()'s finaliser
    // will still run rt_model_close_impl() and clean up correctly even
    // if construction fails partway through this loop.
    for (LiteRtParamIndex i = 0; i < num_inputs; i++) {
        rt_fill_tensor_info(compiled_model, false, signature, i, &self->input_info[i]);
    }
    for (LiteRtParamIndex i = 0; i < num_outputs; i++) {
        rt_fill_tensor_info(compiled_model, true, signature, i, &self->output_info[i]);
    }

    return MP_OBJ_FROM_PTR(self);
}

void raise_if_closed(rt_model_obj_t *self) {
    if (!self->node) {
        raise_os_error(MP_EINVAL, "android.rt: model closed -- call Model() again");
    }
}

// Creates a fresh host-memory tensor buffer matching `info`'s cached
// type/size. Caller owns the returned buffer and must destroy it via
// rt_destroy_buffer_slot() (frees the host memory too; deallocator is
// NULL here, this module owns that memory, not LiteRt).
LiteRtTensorBuffer rt_create_host_buffer(const RtTensorInfo *info) {
    void *host_buf = nullptr;
    if (posix_memalign(&host_buf, LITERT_HOST_MEMORY_BUFFER_ALIGNMENT, info->buffer_size) != 0) {
        raise_os_error(MP_ENOMEM, "android.rt: failed to allocate tensor buffer");
    }
    LiteRtTensorBuffer buf;
    LiteRtStatus status = LiteRtCreateTensorBufferFromHostMemory(
        &info->ranked_type, host_buf, info->buffer_size, nullptr, &buf);
    if (status != kLiteRtStatusOk) {
        free(host_buf);
        raise_os_error(MP_EIO, "android.rt: failed to create tensor buffer");
    }
    return buf;
}

// see session-state: rt_module.cpp#rt_model_set_input_ndarray
mp_obj_t rt_model_set_input_ndarray(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    rt_model_obj_t *self = (rt_model_obj_t *) MP_OBJ_TO_PTR(pos_args[0]);
    raise_if_closed(self);

    enum { ARG_data, ARG_index };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_data, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_index, MP_ARG_INT, {.u_int = 0}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    mp_int_t index = args[ARG_index].u_int;

    if (index < 0 || (LiteRtParamIndex) index >= self->num_inputs) {
        raise_os_error(MP_EINVAL, "android.rt: no input tensor at that index");
    }
    if (!mp_obj_is_type(args[ARG_data].u_obj, &ulab_ndarray_type)) {
        raise_os_error(MP_EINVAL, "android.rt: set_input_ndarray() requires a ulab ndarray");
    }
    ndarray_obj_t *src = (ndarray_obj_t *) MP_OBJ_TO_PTR(args[ARG_data].u_obj);

    const RtTensorInfo *info = &self->input_info[index];
    if (src->len != info->num_elements) {
        raise_os_error(MP_EINVAL, "android.rt: ndarray length does not match the tensor's element count");
    }

    // Replace any previously-set buffer at this index. Idempotent, same
    // as android.tf's own set_input_ndarray() semantics: calling this
    // again before invoke() just overwrites.
    rt_destroy_buffer_slot(&self->input_bufs[index]);
    LiteRtTensorBuffer buf = rt_create_host_buffer(info);

    void *host_ptr = nullptr;
    if (LiteRtLockTensorBuffer(buf, &host_ptr, kLiteRtTensorBufferLockModeWrite) != kLiteRtStatusOk) {
        rt_destroy_buffer_slot(&buf);
        raise_os_error(MP_EIO, "android.rt: failed to lock input tensor buffer");
    }

    LiteRtElementType elem_type = info->ranked_type.element_type;
    size_t len = src->len;

    if (elem_type == kLiteRtElementTypeFloat32) {
        float *dst_f = (float *) host_ptr;
        for (size_t i = 0; i < len; i++) {
            dst_f[i] = (float) ndarray_get_float_index(src->array, src->dtype, i);
        }
    } else {
        // int8/uint8/int16/uint16, checked at cache time in
        // rt_fill_tensor_info(); anything else already raised there.
        if (info->quant_type_id != kLiteRtQuantizationPerTensor) {
            LiteRtUnlockTensorBuffer(buf);
            rt_destroy_buffer_slot(&buf);
            raise_os_error(MP_EINVAL,
                "android.rt: set_input_ndarray() requires per-tensor scale/zero_point; "
                "this tensor has none");
        }
        // real = scale * (raw - zero_point), inverted: raw = value/scale + zero_point.
        float inv_scale = info->quant.scale != 0.0f ? (1.0f / info->quant.scale) : 1.0f;
        float zero_point = (float) info->quant.zero_point;
        for (size_t i = 0; i < len; i++) {
            float v = (float) ndarray_get_float_index(src->array, src->dtype, i);
            float raw = v * inv_scale + zero_point;
            switch (elem_type) {
                case kLiteRtElementTypeInt8: ((int8_t *) host_ptr)[i] = rt_quantize_round_clamp<int8_t>(raw); break;
                case kLiteRtElementTypeUInt8: ((uint8_t *) host_ptr)[i] = rt_quantize_round_clamp<uint8_t>(raw); break;
                case kLiteRtElementTypeInt16: ((int16_t *) host_ptr)[i] = rt_quantize_round_clamp<int16_t>(raw); break;
                case kLiteRtElementTypeUInt16: ((uint16_t *) host_ptr)[i] = rt_quantize_round_clamp<uint16_t>(raw); break;
                default: break;
            }
        }
    }

    LiteRtUnlockTensorBuffer(buf);
    self->input_bufs[index] = buf;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(rt_model_set_input_ndarray_obj, 1, rt_model_set_input_ndarray);

// see session-state: rt_module.cpp#rt_model_invoke
mp_obj_t rt_model_invoke(mp_obj_t self_in) {
    rt_model_obj_t *self = (rt_model_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);

    for (LiteRtParamIndex i = 0; i < self->num_inputs; i++) {
        if (!self->input_bufs[i]) {
            raise_os_error(MP_EINVAL, "android.rt: not every input has been set, call set_input_ndarray() first");
        }
    }

    // Output buffers are recreated fresh on every invoke(). Simpler
    // and safer than trying to reuse stale ones from a previous call.
    for (LiteRtParamIndex i = 0; i < self->num_outputs; i++) {
        rt_destroy_buffer_slot(&self->output_bufs[i]);
        self->output_bufs[i] = rt_create_host_buffer(&self->output_info[i]);
    }

    LiteRtStatus status = LiteRtRunCompiledModel(
        self->compiled_model, /*signature_index=*/0,
        self->num_inputs, self->input_bufs,
        self->num_outputs, self->output_bufs);
    if (status != kLiteRtStatusOk) {
        raise_os_error(MP_EIO, "android.rt: invoke failed");
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(rt_model_invoke_obj, rt_model_invoke);

// ndarray sibling of get_output(). Quantized tensors always
// auto-dequantize to a float ndarray, same OpenMV-style default as
// tf_model_get_output_ndarray(). android.rt has no separate raw-bytes
// get_output(). ulab is already vendored, so there is no reason to also
// expose a struct.pack()-shaped path the way android.tf grew one
// historically.
// see session-state: rt_module.cpp#rt_model_get_output_ndarray
mp_obj_t rt_model_get_output_ndarray(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    rt_model_obj_t *self = (rt_model_obj_t *) MP_OBJ_TO_PTR(pos_args[0]);
    raise_if_closed(self);

    enum { ARG_index };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_index, MP_ARG_INT, {.u_int = 0}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    mp_int_t index = args[ARG_index].u_int;

    if (index < 0 || (LiteRtParamIndex) index >= self->num_outputs) {
        raise_os_error(MP_EINVAL, "android.rt: no output tensor at that index");
    }
    if (!self->output_bufs[index]) {
        raise_os_error(MP_EINVAL, "android.rt: invoke() must be called before get_output_ndarray()");
    }

    const RtTensorInfo *info = &self->output_info[index];
    int32_t ndim = info->ranked_type.layout.rank;
    if (ndim > ULAB_MAX_DIMS) {
        raise_os_error(MP_EINVAL, "android.rt: tensor has more dimensions than ulab supports");
    }
    // ulab shape arrays are right-aligned within a fixed ULAB_MAX_DIMS
    // slots, see ndarray_new_ndarray()'s own internal convention (same
    // as tf_model_get_output_ndarray()).
    size_t shape[ULAB_MAX_DIMS] = {};
    for (int32_t i = 0; i < ndim; i++) {
        shape[ULAB_MAX_DIMS - ndim + i] = (size_t) info->ranked_type.layout.dimensions[i];
    }

    void *host_ptr = nullptr;
    if (LiteRtLockTensorBuffer(self->output_bufs[index], &host_ptr, kLiteRtTensorBufferLockModeRead) != kLiteRtStatusOk) {
        raise_os_error(MP_EIO, "android.rt: failed to lock output tensor buffer");
    }

    LiteRtElementType elem_type = info->ranked_type.element_type;
    ndarray_obj_t *out = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
    float *dst_f = (float *) out->array;

    if (elem_type == kLiteRtElementTypeFloat32) {
        memcpy(dst_f, host_ptr, out->len * sizeof(float));
    } else {
        // Per-tensor-only, already enforced in rt_fill_tensor_info().
        for (size_t i = 0; i < out->len; i++) {
            float raw;
            switch (elem_type) {
                case kLiteRtElementTypeInt8: raw = ((const int8_t *) host_ptr)[i]; break;
                case kLiteRtElementTypeUInt8: raw = ((const uint8_t *) host_ptr)[i]; break;
                case kLiteRtElementTypeInt16: raw = ((const int16_t *) host_ptr)[i]; break;
                case kLiteRtElementTypeUInt16: raw = ((const uint16_t *) host_ptr)[i]; break;
                default: raw = 0; break;
            }
            dst_f[i] = (raw - (float) info->quant.zero_point) * info->quant.scale;
        }
    }

    LiteRtUnlockTensorBuffer(self->output_bufs[index]);
    return MP_OBJ_FROM_PTR(out);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(rt_model_get_output_ndarray_obj, 1, rt_model_get_output_ndarray);

// LiteRtElementType -> canonical dtype name string. Same numpy/ML-
// ecosystem naming as tf_dtype_name() in tf_module.cpp. Values are
// matched to tflite_types.h 1:1 (litert_model_types.h's own comment).
const char *rt_dtype_name(LiteRtElementType type, char *fallback_buf, size_t fallback_buf_size) {
    switch (type) {
        case kLiteRtElementTypeNone: return "no_type";
        case kLiteRtElementTypeFloat32: return "float32";
        case kLiteRtElementTypeInt32: return "int32";
        case kLiteRtElementTypeUInt8: return "uint8";
        case kLiteRtElementTypeInt64: return "int64";
        case kLiteRtElementTypeTfString: return "string";
        case kLiteRtElementTypeBool: return "bool";
        case kLiteRtElementTypeInt16: return "int16";
        case kLiteRtElementTypeComplex64: return "complex64";
        case kLiteRtElementTypeInt8: return "int8";
        case kLiteRtElementTypeFloat16: return "float16";
        case kLiteRtElementTypeFloat64: return "float64";
        case kLiteRtElementTypeComplex128: return "complex128";
        case kLiteRtElementTypeUInt64: return "uint64";
        case kLiteRtElementTypeTfResource: return "resource";
        case kLiteRtElementTypeTfVariant: return "variant";
        case kLiteRtElementTypeUInt32: return "uint32";
        case kLiteRtElementTypeUInt16: return "uint16";
        case kLiteRtElementTypeInt4: return "int4";
        case kLiteRtElementTypeBFloat16: return "bfloat16";
        case kLiteRtElementTypeInt2: return "int2";
        case kLiteRtElementTypeUInt4: return "uint4";
        case kLiteRtElementTypeFloat8E4M3FN: return "float8e4m3fn";
        case kLiteRtElementTypeFloat8E5M2: return "float8e5m2";
        default:
            snprintf(fallback_buf, fallback_buf_size, "unknown(%d)", (int) type);
            return fallback_buf;
    }
}

// One dict per tensor: {'name', 'shape', 'dtype', 'bytes', 'scale', 'zero_point'}.
// Mirrors tf_tensor_info_dict()'s own shape exactly.
// see session-state: tf_module.cpp#tf_tensor_info_dict
mp_obj_t rt_tensor_info_dict(LiteRtSignature signature, bool is_output, LiteRtParamIndex index,
                              const RtTensorInfo *info) {
    LiteRtTensor tensor;
    LiteRtStatus status = is_output
        ? LiteRtGetSignatureOutputTensorByIndex(signature, index, &tensor)
        : LiteRtGetSignatureInputTensorByIndex(signature, index, &tensor);

    mp_obj_t dict = mp_obj_new_dict(6);

    const char *name = "";
    if (status == kLiteRtStatusOk) {
        LiteRtGetTensorName(tensor, &name);
    }
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_name), mp_obj_new_str(name, strlen(name)));

    // Already fully concrete. rt_fill_tensor_info() overwrites any
    // dynamic dim in place, see RtTensorInfo's own comment.
    mp_obj_t shape = mp_obj_new_list(0, nullptr);
    int32_t rank = info->ranked_type.layout.rank;
    for (int32_t i = 0; i < rank; i++) {
        mp_obj_list_append(shape, mp_obj_new_int(info->ranked_type.layout.dimensions[i]));
    }
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_shape), shape);

    char fallback_buf[24];
    const char *dtype = rt_dtype_name(info->ranked_type.element_type, fallback_buf, sizeof(fallback_buf));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_dtype), mp_obj_new_str(dtype, strlen(dtype)));

    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_bytes), mp_obj_new_int_from_uint(info->buffer_size));

    float scale = info->quant_type_id == kLiteRtQuantizationPerTensor ? info->quant.scale : 0.0f;
    int64_t zero_point = info->quant_type_id == kLiteRtQuantizationPerTensor ? info->quant.zero_point : 0;
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_scale), mp_obj_new_float(scale));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_zero_point), mp_obj_new_int(zero_point));

    return dict;
}

mp_obj_t rt_model_info(mp_obj_t self_in) {
    rt_model_obj_t *self = (rt_model_obj_t *) MP_OBJ_TO_PTR(self_in);
    raise_if_closed(self);

    mp_obj_t inputs = mp_obj_new_list(0, nullptr);
    for (LiteRtParamIndex i = 0; i < self->num_inputs; i++) {
        mp_obj_list_append(inputs, rt_tensor_info_dict(self->signature, false, i, &self->input_info[i]));
    }

    mp_obj_t outputs = mp_obj_new_list(0, nullptr);
    for (LiteRtParamIndex i = 0; i < self->num_outputs; i++) {
        mp_obj_list_append(outputs, rt_tensor_info_dict(self->signature, true, i, &self->output_info[i]));
    }

    mp_obj_t dict = mp_obj_new_dict(2);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_inputs), inputs);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_outputs), outputs);
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_1(rt_model_info_obj, rt_model_info);

mp_obj_t rt_model_close(mp_obj_t self_in) {
    rt_model_close_impl((rt_model_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(rt_model_close_obj, rt_model_close);

// __del__. Only reached by py/gc.c's finaliser sweep, never called
// directly by a script.
mp_obj_t rt_model_del(mp_obj_t self_in) {
    rt_model_close_impl((rt_model_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(rt_model_del_obj, rt_model_del);

const mp_rom_map_elem_t rt_model_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_set_input_ndarray), MP_ROM_PTR(&rt_model_set_input_ndarray_obj)},
    {MP_ROM_QSTR(MP_QSTR_invoke), MP_ROM_PTR(&rt_model_invoke_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_output_ndarray), MP_ROM_PTR(&rt_model_get_output_ndarray_obj)},
    {MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&rt_model_info_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&rt_model_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&rt_model_del_obj)},
};
MP_DEFINE_CONST_DICT(rt_model_locals_dict, rt_model_locals_dict_table);

} // namespace

// extern prefix required, same MP_DEFINE_CONST_OBJ_TYPE internal-linkage
// reasoning as tf_model_type in tf_module.cpp.
extern MP_DEFINE_CONST_OBJ_TYPE(
    rt_model_type,
    MP_QSTR_Model,
    MP_TYPE_FLAG_NONE,
    make_new, rt_model_make_new,
    locals_dict, &rt_model_locals_dict
    );

extern "C" void rt_close_all(void) {
    // Every registered Model's native handles first. The environment
    // must outlive every compiled model created from it.
    // see session-state: rt_module.cpp#environment_lifecycle
    while (g_rt_models) {
        RtModelRegistryNode *node = g_rt_models;
        g_rt_models = node->next;
        LiteRtDestroyCompiledModel(node->compiled_model);
        LiteRtDestroyModel(node->model);
        free(node);
    }
    // Environment last, then reset to null so a fresh one is created
    // lazily the next time a script calls Model() after this reset.
    if (g_rt_environment) {
        LiteRtDestroyEnvironment(g_rt_environment);
        g_rt_environment = nullptr;
    }
}

// android.rt.info() returns {'accelerators': [str, ...]}, the real,
// currently-registered accelerator names on the shared environment
// (empty list, and an empty environment, before the first Model() call).
// A scriptable way to confirm GPU accelerator registration, not just a
// logcat line.
// see session-state: rt_module.cpp#android_rt_info
// Declared outside the anonymous namespace so android_module.cpp can
// reference the function object (extern prefix required, same linkage
// reasoning as tf_module.cpp's android_tf_info_obj).
mp_obj_t android_rt_info() {
    mp_obj_t accelerators = mp_obj_new_list(0, nullptr);
    if (g_rt_environment) {
        LiteRtParamIndex count = 0;
        LiteRtGetNumAccelerators(g_rt_environment, &count);
        for (LiteRtParamIndex i = 0; i < count; i++) {
            LiteRtAccelerator accelerator;
            if (LiteRtGetAccelerator(g_rt_environment, i, &accelerator) != kLiteRtStatusOk) {
                continue;
            }
            const char *name = "";
            LiteRtGetAcceleratorName(accelerator, &name);
            mp_obj_list_append(accelerators, mp_obj_new_str(name, strlen(name)));
        }
    }
    mp_obj_t dict = mp_obj_new_dict(1);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_accelerators), accelerators);
    return dict;
}
extern MP_DEFINE_CONST_FUN_OBJ_0(android_rt_info_obj, android_rt_info);
