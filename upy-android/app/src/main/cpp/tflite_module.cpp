// upy-android native tflite module (top-level `tflite`). Classic
// TensorFlow Lite C API, CPU-only (no delegate is ever attached here).
// Links the SAME libLiteRt.so the litert module links -- see
// CMakeLists.txt's comment on why one shared native library, not two
// competing copies of TFLite in one process. Zero cross-links with
// litert_module.cpp/LiteRtShim.kt: independent source, independent
// Python-facing API, each with its own registry/teardown below.
// see session-state: tflite_module.cpp#module_design

#include <cstdlib>
#include <cstring>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"
// ndarray.h has no extern "C" guards of its own (ulab is a plain-C
// library); wrapped here so ndarray_new_dense_ndarray() etc. resolve to
// ndarray.c's actual (unmangled) C symbols at link time, not C++-mangled
// names nothing defines. Confirmed the hard way: this project's first
// .cpp to include ndarray.h directly, originally unwrapped, failed at
// link with "undefined symbol: ndarray_new_dense_ndarray(...)".
#include "ndarray.h"
}

#include "tflite/c/c_api.h"

extern "C" const mp_obj_type_t ulab_ndarray_type;

namespace {

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// Engine-reset teardown registry. Deliberately a separate, independent
// implementation from litert_module.cpp's own LitertHandleNode (same
// shape, no shared code -- see this file's own header comment).
struct TfliteModelNode {
    TfLiteModel *model;
    TfLiteInterpreter *interp;
    TfliteModelNode *next;
};
TfliteModelNode *g_tflite_models = nullptr;

void registry_add(TfLiteModel *model, TfLiteInterpreter *interp) {
    auto *node = (TfliteModelNode *) malloc(sizeof(TfliteModelNode));
    node->model = model;
    node->interp = interp;
    node->next = g_tflite_models;
    g_tflite_models = node;
}

void registry_remove(TfLiteModel *model, TfLiteInterpreter *interp) {
    TfliteModelNode **link = &g_tflite_models;
    while (*link) {
        if ((*link)->model == model && (*link)->interp == interp) {
            TfliteModelNode *node = *link;
            *link = node->next;
            free(node);
            return;
        }
        link = &(*link)->next;
    }
}

// v1 dtype set: no quantization abstraction, no implicit conversion --
// a tensor's dtype must exactly match the ndarray's dtype, byte for
// byte. int32/int64/bool are deferred (ulab has no matching dtype yet,
// see session-state).
struct DtypeEntry {
    TfLiteType tfl;
    uint8_t ulab;
    const char *name;
};
const DtypeEntry kDtypeTable[] = {
    {kTfLiteFloat32, NDARRAY_FLOAT, "float32"},
    {kTfLiteInt8, NDARRAY_INT8, "int8"},
    {kTfLiteUInt8, NDARRAY_UINT8, "uint8"},
    {kTfLiteInt16, NDARRAY_INT16, "int16"},
    {kTfLiteUInt16, NDARRAY_UINT16, "uint16"},
};

uint8_t tfl_to_ulab_dtype(TfLiteType t, const char **name_out) {
    for (const auto &e : kDtypeTable) {
        if (e.tfl == t) {
            if (name_out) {
                *name_out = e.name;
            }
            return e.ulab;
        }
    }
    return 0;
}

typedef struct _tflite_model_obj_t {
    mp_obj_base_t base;
    TfLiteModel *model;
    TfLiteInterpreter *interp;  // nullptr once closed
} tflite_model_obj_t;

void tflite_close(tflite_model_obj_t *self) {
    if (!self->interp) {
        return;
    }
    registry_remove(self->model, self->interp);
    TfLiteInterpreterDelete(self->interp);
    TfLiteModelDelete(self->model);
    self->interp = nullptr;
    self->model = nullptr;
}

mp_obj_t tflite_model_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_path };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_path, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    const char *path = mp_obj_str_get_str(parsed[ARG_path].u_obj);
    // TfLiteModelCreateFromFile is a real fopen(), not VFS-aware -- same
    // underlying reason as litert_module.cpp's identical-in-spirit
    // leading-slash strip (independent code, not shared): this port's
    // VFS root is mounted at "/", but the real filesystem cwd is
    // already the app's private storage.
    if (path[0] == '/') {
        path++;
    }

    TfLiteModel *model = TfLiteModelCreateFromFile(path);
    if (!model) {
        raise_os_error(MP_ENOENT, "tflite: could not load model (bad path or invalid .tflite file)");
    }

    TfLiteInterpreterOptions *options = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreter *interp = TfLiteInterpreterCreate(model, options);
    TfLiteInterpreterOptionsDelete(options);
    if (!interp) {
        TfLiteModelDelete(model);
        raise_os_error(MP_EIO, "tflite: failed to create interpreter");
    }
    if (TfLiteInterpreterAllocateTensors(interp) != kTfLiteOk) {
        TfLiteInterpreterDelete(interp);
        TfLiteModelDelete(model);
        raise_os_error(MP_EIO, "tflite: failed to allocate tensors");
    }

    auto *self = m_new_obj(tflite_model_obj_t);
    self->base.type = type;
    self->model = model;
    self->interp = interp;
    registry_add(model, interp);
    return MP_OBJ_FROM_PTR(self);
}

// Copies ndarray `arr_obj`'s buffer into TFLite input tensor `index`.
// Strict: dtype and byte size must match exactly, no conversion -- no
// quantization abstraction, no implicit cast (see session-state).
void copy_input(TfLiteInterpreter *interp, int32_t index, mp_obj_t arr_obj) {
    if (!mp_obj_is_type(arr_obj, &ulab_ndarray_type)) {
        raise_os_error(MP_EINVAL, "tflite: run() arguments must be ulab ndarrays");
    }
    TfLiteTensor *tensor = TfLiteInterpreterGetInputTensor(interp, index);
    const char *name = nullptr;
    uint8_t want_dtype = tfl_to_ulab_dtype(TfLiteTensorType(tensor), &name);
    if (want_dtype == 0) {
        raise_os_error(MP_EINVAL,
            "tflite: input tensor dtype not supported in this version (only float32/int8/uint8/int16/uint16)");
    }
    auto *arr = (ndarray_obj_t *) MP_OBJ_TO_PTR(arr_obj);
    if (arr->dtype != want_dtype) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("tflite: input %d dtype mismatch, tensor wants %s"),
            (int) index, name);
    }
    mp_buffer_info_t bufinfo;
    // mp_get_buffer_raise rejects non-contiguous views -- a success
    // here IS the "safe to hand this pointer straight to TfLite" signal
    // (see session-state: ulab ndarray audit).
    mp_get_buffer_raise(arr_obj, &bufinfo, MP_BUFFER_READ);
    size_t tensor_bytes = TfLiteTensorByteSize(tensor);
    if (bufinfo.len != tensor_bytes) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("tflite: input %d size mismatch: array is %u bytes, tensor wants %u"),
            (int) index, (unsigned) bufinfo.len, (unsigned) tensor_bytes);
    }
    if (TfLiteTensorCopyFromBuffer(tensor, bufinfo.buf, bufinfo.len) != kTfLiteOk) {
        raise_os_error(MP_EIO, "tflite: failed to copy input into tensor");
    }
}

// Allocates a fresh, GC-owned ndarray and copies TFLite output tensor
// into it. Always a copy in v1: wrapping the interpreter's own arena
// buffer zero-copy would dangle across the next invoke() or close() --
// deferred to v2 (see session-state).
mp_obj_t copy_output(const TfLiteTensor *tensor) {
    const char *name = nullptr;
    uint8_t dtype = tfl_to_ulab_dtype(TfLiteTensorType(tensor), &name);
    if (dtype == 0) {
        raise_os_error(MP_EINVAL,
            "tflite: output tensor dtype not supported in this version (only float32/int8/uint8/int16/uint16)");
    }
    int32_t ndim = TfLiteTensorNumDims(tensor);
    if (ndim < 1 || ndim > ULAB_MAX_DIMS) {
        raise_os_error(MP_EINVAL, "tflite: output tensor rank not supported");
    }
    // ndarray_new_ndarray() reads the TRAILING ndim slots of a full
    // ULAB_MAX_DIMS-length shape array (right-aligned, like
    // ndarray_new_ndarray_from_tuple's own construction) -- confirmed
    // by reading ndarray.c directly, not assumed. A left-aligned fill
    // here silently produced a 0-length/wrong-length array (caught by
    // the byte-size check below) rather than a crash.
    size_t shape[ULAB_MAX_DIMS] = {0};
    for (int32_t i = 0; i < ndim; i++) {
        shape[ULAB_MAX_DIMS - ndim + i] = (size_t) TfLiteTensorDim(tensor, i);
    }
    ndarray_obj_t *out = ndarray_new_dense_ndarray((uint8_t) ndim, shape, dtype);
    size_t bytes = TfLiteTensorByteSize(tensor);
    if (bytes != out->len * out->itemsize) {
        raise_os_error(MP_EIO, "tflite: output tensor size mismatch");
    }
    if (TfLiteTensorCopyToBuffer(tensor, out->array, bytes) != kTfLiteOk) {
        raise_os_error(MP_EIO, "tflite: failed to copy output from tensor");
    }
    return MP_OBJ_FROM_PTR(out);
}

// run(*inputs): one positional ndarray per input tensor, in order.
// Returns a single ndarray if the model has exactly one output tensor,
// otherwise a tuple -- matches the common case (most models, including
// add_simple.tflite) without forcing "(y,) = model.run(x)" everywhere.
mp_obj_t tflite_model_run(size_t n_args, const mp_obj_t *args) {
    auto *self = (tflite_model_obj_t *) MP_OBJ_TO_PTR(args[0]);
    if (!self->interp) {
        raise_os_error(MP_EINVAL, "tflite: model is closed");
    }
    int32_t want = TfLiteInterpreterGetInputTensorCount(self->interp);
    if ((int32_t) (n_args - 1) != want) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("tflite: run() needs %d input(s), got %d"),
            (int) want, (int) (n_args - 1));
    }
    for (int32_t i = 0; i < want; i++) {
        copy_input(self->interp, i, args[1 + i]);
    }
    if (TfLiteInterpreterInvoke(self->interp) != kTfLiteOk) {
        raise_os_error(MP_EIO, "tflite: inference failed");
    }
    int32_t n_out = TfLiteInterpreterGetOutputTensorCount(self->interp);
    if (n_out == 1) {
        return copy_output(TfLiteInterpreterGetOutputTensor(self->interp, 0));
    }
    mp_obj_t *items = m_new(mp_obj_t, n_out);
    for (int32_t i = 0; i < n_out; i++) {
        items[i] = copy_output(TfLiteInterpreterGetOutputTensor(self->interp, i));
    }
    return mp_obj_new_tuple(n_out, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tflite_model_run_obj, 1, MP_OBJ_FUN_ARGS_MAX, tflite_model_run);

mp_obj_t tflite_model_close(mp_obj_t self_in) {
    tflite_close((tflite_model_obj_t *) MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(tflite_model_close_obj, tflite_model_close);

const mp_rom_map_elem_t tflite_model_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&tflite_model_run_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&tflite_model_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&tflite_model_close_obj)},
};
MP_DEFINE_CONST_DICT(tflite_model_locals_dict, tflite_model_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    tflite_model_type,
    MP_QSTR_Model,
    MP_TYPE_FLAG_NONE,
    make_new, tflite_model_make_new,
    locals_dict, &tflite_model_locals_dict
    );

// Keep this in sync with tflite_model_locals_dict_table/
// tflite_module_globals_table above -- see DEVELOPER.md's "adb server"
// section.
const char tflite_help_text[] =
    "module: tflite\n"
    "class: tflite.Model(path)\n"
    "  loads a .tflite model and allocates tensors immediately; path is a VFS path (e.g. \"/my_model.tflite\")\n"
    "methods:\n"
    "  run(*inputs): one positional ulab.numpy ndarray per input tensor, in the model's own order\n"
    "    strict: each array's dtype and byte size must exactly match the corresponding tensor (no quantization/scale/zero_point handling, no implicit cast)\n"
    "    returns a single ndarray if the model has one output tensor, otherwise a tuple of ndarrays\n"
    "    always allocates fresh output arrays (no zero-copy in this version)\n"
    "  close(): frees the model and interpreter; also called automatically on garbage collection\n"
    "dtypes_supported: float32, int8, uint8, int16, uint16 (matching ulab's own dtype set; int32/int64/bool are not yet supported)\n"
    "errors:\n"
    "  OSError(ENOENT): model file not found or not a valid .tflite file\n"
    "  OSError(EIO): interpreter creation, tensor allocation, inference, or tensor copy failed\n"
    "  OSError(EINVAL): called on a closed model, or a tensor dtype is outside dtypes_supported\n"
    "  ValueError: wrong number of run() arguments, or an input array's dtype/size does not match its tensor\n"
    "notes:\n"
    "  CPU-only: no GPU/NPU delegate is ever attached by this module (see litert.help() for accelerator-capable inference)\n"
    "see_also: litert.help()\n"
;
mp_obj_t tflite_help() {
    return mp_obj_new_str(tflite_help_text, strlen(tflite_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(tflite_help_obj, tflite_help);

const mp_rom_map_elem_t tflite_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_tflite)},
    {MP_ROM_QSTR(MP_QSTR_Model), MP_ROM_PTR(&tflite_model_type)},
    {MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&tflite_help_obj)},
};
MP_DEFINE_CONST_DICT(tflite_module_globals, tflite_module_globals_table);

} // namespace

extern "C" const mp_obj_module_t tflite_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &tflite_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_tflite, tflite_module);

extern "C" void tflite_close_all(void) {
    while (g_tflite_models) {
        TfliteModelNode *node = g_tflite_models;
        TfLiteInterpreterDelete(node->interp);
        TfLiteModelDelete(node->model);
        g_tflite_models = node->next;
        free(node);
    }
}
