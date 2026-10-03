// upy-android native tflite module (top-level `tflite`). Classic
// TensorFlow Lite C API, CPU by default; set_nnapi(True) hands new models
// to Android NNAPI.
// Links the SAME libLiteRt.so the litert module links -- see
// CMakeLists.txt's comment on why one shared native library, not two
// competing copies of TFLite in one process. Zero cross-links with
// litert_module.cpp/LiteRtShim.kt: independent source, independent
// Python-facing API, each with its own registry/teardown below.
//
// I/O is plain array.array: a MicroPython builtin (no new dependency)
// whose typecodes cover the real TfLiteType range directly, raw bytes
// copied straight into/out of the tensor.
// see session-state: tflite_module.cpp#module_design

#include <cstdlib>
#include <cstring>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/binary.h"
#include "py/mperrno.h"
}

#include "tflite/c/c_api.h"

// From tflite's c_api_experimental.h, exported by libLiteRt.so.
extern "C" void TfLiteInterpreterOptionsSetUseNNAPI(TfLiteInterpreterOptions *options, bool enable);

namespace {

// set_nnapi(): applies to models created afterwards.
bool g_use_nnapi = false;

void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// Engine-reset teardown registry.
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

// No quantization abstraction, no implicit conversion -- a tensor's
// dtype must exactly match the array's typecode, byte for byte (see
// session-state). Covers the real, practical TfLiteType range; bool is
// represented as plain 0/1 uint8 bytes (array.array has no bool
// typecode of its own). float16/float64/complex/string/resource/
// variant are out of scope -- rare in this context, not a hard
// technical limit, can be added if a real model needs one.
struct DtypeEntry {
    TfLiteType tfl;
    char typecode;
    const char *name;
};
const DtypeEntry kDtypeTable[] = {
    {kTfLiteFloat32, 'f', "float32"},
    {kTfLiteInt8, 'b', "int8"},
    {kTfLiteUInt8, 'B', "uint8"},
    {kTfLiteInt16, 'h', "int16"},
    {kTfLiteUInt16, 'H', "uint16"},
    {kTfLiteInt32, 'i', "int32"},
    {kTfLiteUInt32, 'I', "uint32"},
    {kTfLiteInt64, 'q', "int64"},
    {kTfLiteUInt64, 'Q', "uint64"},
    {kTfLiteBool, 'B', "bool (as uint8 0/1)"},
};

char tfl_typecode(TfLiteType t, const char **name_out) {
    for (const auto &e : kDtypeTable) {
        if (e.tfl == t) {
            if (name_out) {
                *name_out = e.name;
            }
            return e.typecode;
        }
    }
    return '\0';
}

// Allocates a fresh, GC-owned array.array of the given typecode/length,
// built from the same public primitives array_make_new() itself uses
// (mp_binary_get_size for itemsize, m_new for the GC-owned backing
// buffer). array_new() itself (objarray.c) is static, not exported, so
// this is a small, deliberate duplicate, not a shared/cross-linked
// implementation.
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
    // TfLiteModelCreateFromFile is a real fopen(), not VFS-aware: this
    // port's VFS root is mounted at "/", but the real filesystem cwd is
    // already the app's private storage.
    if (path[0] == '/') {
        path++;
    }

    TfLiteModel *model = TfLiteModelCreateFromFile(path);
    if (!model) {
        raise_os_error(MP_ENOENT, "tflite: could not load model (bad path or invalid .tflite file)");
    }

    TfLiteInterpreterOptions *options = TfLiteInterpreterOptionsCreate();
    if (g_use_nnapi) {
        TfLiteInterpreterOptionsSetUseNNAPI(options, true);
    }
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

// Builds a tuple of this tensor's dimensions -- the input_shape()/
// output_shape() methods below, and run()'s own dtype/size checks.
mp_obj_t tensor_shape_tuple(const TfLiteTensor *tensor) {
    int32_t ndim = TfLiteTensorNumDims(tensor);
    mp_obj_t *items = m_new(mp_obj_t, ndim);
    for (int32_t i = 0; i < ndim; i++) {
        items[i] = mp_obj_new_int(TfLiteTensorDim(tensor, i));
    }
    return mp_obj_new_tuple(ndim, items);
}

mp_obj_t tflite_model_input_shape(mp_obj_t self_in, mp_obj_t index_in) {
    auto *self = (tflite_model_obj_t *) MP_OBJ_TO_PTR(self_in);
    if (!self->interp) {
        raise_os_error(MP_EINVAL, "tflite: model is closed");
    }
    mp_int_t index = mp_obj_get_int(index_in);
    if (index < 0 || index >= TfLiteInterpreterGetInputTensorCount(self->interp)) {
        raise_os_error(MP_EINVAL, "tflite: no such input tensor index");
    }
    return tensor_shape_tuple(TfLiteInterpreterGetInputTensor(self->interp, (int32_t) index));
}
static MP_DEFINE_CONST_FUN_OBJ_2(tflite_model_input_shape_obj, tflite_model_input_shape);

mp_obj_t tflite_model_output_shape(mp_obj_t self_in, mp_obj_t index_in) {
    auto *self = (tflite_model_obj_t *) MP_OBJ_TO_PTR(self_in);
    if (!self->interp) {
        raise_os_error(MP_EINVAL, "tflite: model is closed");
    }
    mp_int_t index = mp_obj_get_int(index_in);
    if (index < 0 || index >= TfLiteInterpreterGetOutputTensorCount(self->interp)) {
        raise_os_error(MP_EINVAL, "tflite: no such output tensor index");
    }
    return tensor_shape_tuple(TfLiteInterpreterGetOutputTensor(self->interp, (int32_t) index));
}
static MP_DEFINE_CONST_FUN_OBJ_2(tflite_model_output_shape_obj, tflite_model_output_shape);

// Copies array.array `arr_obj`'s buffer into TFLite input tensor
// `index`. Strict: typecode and byte size must match exactly, no
// conversion -- no quantization abstraction, no implicit cast (see
// session-state).
void copy_input(TfLiteInterpreter *interp, int32_t index, mp_obj_t arr_obj) {
    if (!mp_obj_is_type(arr_obj, &mp_type_array)) {
        raise_os_error(MP_EINVAL, "tflite: run() arguments must be array.array");
    }
    TfLiteTensor *tensor = TfLiteInterpreterGetInputTensor(interp, index);
    const char *name = nullptr;
    char want_typecode = tfl_typecode(TfLiteTensorType(tensor), &name);
    if (want_typecode == '\0') {
        raise_os_error(MP_EINVAL, "tflite: input tensor dtype not supported in this version");
    }
    mp_buffer_info_t bufinfo;
    // mp_get_buffer_raise rejects non-contiguous views -- a success
    // here IS the "safe to hand this pointer straight to TfLite" signal.
    mp_get_buffer_raise(arr_obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.typecode != want_typecode) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("tflite: input %d dtype mismatch, tensor wants %s"),
            (int) index, name);
    }
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

// Allocates a fresh, GC-owned array.array and copies TFLite output
// tensor into it. Always a copy in v1: wrapping the interpreter's own
// arena buffer zero-copy would dangle across the next invoke() or
// close() -- deferred, see session-state. Flat (1-D): shape is a
// separate query (output_shape()), not carried by run()'s own return
// value.
mp_obj_t copy_output(const TfLiteTensor *tensor) {
    const char *name = nullptr;
    char typecode = tfl_typecode(TfLiteTensorType(tensor), &name);
    if (typecode == '\0') {
        raise_os_error(MP_EINVAL, "tflite: output tensor dtype not supported in this version");
    }
    size_t bytes = TfLiteTensorByteSize(tensor);
    size_t itemsize = mp_binary_get_size('@', typecode, nullptr);
    mp_obj_array_t *out = new_typed_array(typecode, bytes / itemsize);
    if (TfLiteTensorCopyToBuffer(tensor, out->items, bytes) != kTfLiteOk) {
        raise_os_error(MP_EIO, "tflite: failed to copy output from tensor");
    }
    return MP_OBJ_FROM_PTR(out);
}

// run(*inputs): one positional array.array per input tensor, in order.
// Returns a single array.array if the model has exactly one output
// tensor, otherwise a tuple -- matches the common case (most models,
// including add_simple.tflite) without forcing "(y,) = model.run(x)"
// everywhere.
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
    {MP_ROM_QSTR(MP_QSTR_input_shape), MP_ROM_PTR(&tflite_model_input_shape_obj)},
    {MP_ROM_QSTR(MP_QSTR_output_shape), MP_ROM_PTR(&tflite_model_output_shape_obj)},
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
    "  run(*inputs): one positional array.array per input tensor, in the model's own order\n"
    "    strict: each array's typecode and byte size must exactly match the corresponding tensor (no quantization/scale/zero_point handling, no implicit cast)\n"
    "    returns a single array.array if the model has one output tensor, otherwise a tuple of array.array; always flat (1-D), always a fresh copy\n"
    "  input_shape(i)/output_shape(i): tuple of dimensions for input/output tensor i; run()'s own arrays carry no shape, query it here\n"
    "  close(): frees the model and interpreter; also called automatically on garbage collection\n"
    "functions:\n"
    "  set_nnapi(enable): True hands models created afterwards to Android NNAPI, False (default) uses the CPU; no argument returns the current setting; reset to False by an interpreter reset\n"
    "dtypes_supported: float32('f'), int8('b'), uint8('B'), int16('h'), uint16('H'), int32('i'), uint32('I'), int64('q'), uint64('Q'), bool (as uint8 'B', 0/1)\n"
    "errors:\n"
    "  OSError(ENOENT): model file not found or not a valid .tflite file\n"
    "  OSError(EIO): interpreter creation, tensor allocation, inference, or tensor copy failed\n"
    "  OSError(EINVAL): called on a closed model, a bad tensor index, or a tensor dtype outside dtypes_supported\n"
    "  ValueError: wrong number of run() arguments, or an input array's typecode/size does not match its tensor\n"
    "notes:\n"
    "  NNAPI is deprecated since Android 15 and may silently fall back to the CPU; compare speed to see if it helps (see litert.help() for accelerator-capable inference)\n"
    "see_also: litert.help()\n"
;
mp_obj_t tflite_help() {
    return mp_obj_new_str(tflite_help_text, strlen(tflite_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(tflite_help_obj, tflite_help);

mp_obj_t tflite_set_nnapi(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return mp_obj_new_bool(g_use_nnapi);
    }
    if (mp_obj_is_type(args[0], &mp_type_list) || mp_obj_is_type(args[0], &mp_type_tuple)) {
        mp_raise_ValueError(MP_ERROR_TEXT("tflite: NNAPI device selection not supported yet"));
    }
    g_use_nnapi = mp_obj_is_true(args[0]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tflite_set_nnapi_obj, 0, 1, tflite_set_nnapi);

const mp_rom_map_elem_t tflite_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_tflite)},
    {MP_ROM_QSTR(MP_QSTR_Model), MP_ROM_PTR(&tflite_model_type)},
    {MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&tflite_help_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_nnapi), MP_ROM_PTR(&tflite_set_nnapi_obj)},
};
MP_DEFINE_CONST_DICT(tflite_module_globals, tflite_module_globals_table);

} // namespace

extern "C" const mp_obj_module_t tflite_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &tflite_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_tflite, tflite_module);

extern "C" void tflite_close_all(void) {
    g_use_nnapi = false;
    while (g_tflite_models) {
        TfliteModelNode *node = g_tflite_models;
        TfLiteInterpreterDelete(node->interp);
        TfLiteModelDelete(node->model);
        g_tflite_models = node->next;
        free(node);
    }
}
