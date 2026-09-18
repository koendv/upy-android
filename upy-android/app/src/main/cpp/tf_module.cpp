// upy-android native tf module -- OUR OWN code, NOT vendored OpenMV
// source. android.tf.Model -- real API surface (see SESSION_STATE.yaml's
// "android.tf" design discussion for the full trail), replacing the
// version()-only mockup this file started as. android.tf.version()
// itself is kept (cheap, harmless, still a real diagnostic).
//
// DESIGN, as settled in discussion + a targeted advisor review before
// writing any of this:
//
// - Multi-instance type (android.tf.Model(path)), NOT a csi/light/zoom-
//   style singleton wrapping one global resource -- a phone can hold
//   several loaded models at once, there's no physical one-device
//   constraint the way there is for the camera/flash/sensor.
// - Split calls (set_input()/invoke()/get_output()) mirroring the C API
//   directly, matching this project's established "mirror the calls,
//   don't invent abstractions" precedent (csi.CSI(cid=...) etc) -- not a
//   single run() convenience wrapper. set_input()'s call boundary is
//   also exactly where a future Phase 1 tensor-aliasing implementation
//   (TfLiteInterpreterSetCustomAllocationForTensor, see SESSION_STATE.
//   yaml) slots in later without reshaping this API.
// - Constructor does everything eagerly (load model, create interpreter
//   + delegate options, allocate tensors) and raises immediately on any
//   failure -- no partially-initialized object is ever returned or
//   registered.
// - NNAPI: TfLiteInterpreterOptionsSetUseNNAPI() +
//   SetEnableDelegateFallback(), NOT a manually-created delegate object
//   -- confirmed via `nm -D` on the real libLiteRt.so that the legacy
//   TfLiteNnapiDelegateCreate() entry point this project first assumed
//   does NOT exist in this build; these two simpler options-flag calls
//   do. Best-effort by construction: SetEnableDelegateFallback(true)
//   means a failed/rejected NNAPI attempt falls back to CPU/XNNPACK
//   automatically, never a script-visible error -- matches this
//   project's established best-effort philosophy (android.light etc),
//   and NO claim is made anywhere here about real hardware acceleration
//   actually being used -- unverified/unbenchmarked, same standing
//   caution as every other NNAPI mention in this project.
//
// REGISTRY / close_all(), the one genuinely new pattern in this
// codebase, gotten deliberately conservative after an advisor review
// flagged the obvious-in-hindsight failure mode: an intrusive linked
// list threaded through the mp_obj_t structs themselves (the naive
// design) would store raw pointers to GC-heap objects in a plain C
// global -- invisible to the GC, so a Model with no remaining Python
// references could be collected while still linked into the registry,
// leaving tf_close_all() walking a dangling pointer the next time
// reset() runs. This project already has a working, established
// pattern for the same class of problem (MICROPY_ENABLE_FINALISER,
// confirmed ON in mpconfigport.h) -- a genuine Python-visible __del__
// method, invoked safely BY the GC itself when an unreferenced Model
// is actually collected, exactly like py/gc.c's own finaliser sweep
// (mp_load_method_maybe(obj, MP_QSTR___del__, ...)) already does for
// any other type in this codebase that wants one, none has yet. That
// handles the "script created a Model, dropped it, never called
// close()" case correctly and safely, during ordinary operation.
//
// It does NOT handle the reset()-time case (matching why
// camera_close_all()/imu_close_all() exist at all rather than relying on
// GC): reset() must deterministically free every open Model's native
// LiteRT memory (a real tensor arena, potentially tens of MB) RIGHT NOW,
// not whenever the GC next happens to run a sweep -- and a Model still
// bound to a live Python name wouldn't even be collectible yet at that
// point. So a registry is still needed, just not one that touches the
// GC heap: TfModelRegistryNode below holds ONLY plain native handles
// (TfLiteInterpreter*/TfLiteModel*), malloc'd/freed with plain
// malloc/free, entirely outside anything the GC traces. tf_close_all()
// walks this native-only list and calls TfLiteInterpreterDelete/
// TfLiteModelDelete directly -- it never touches an mp_obj_t at all, so
// there is nothing here for a concurrent GC or a subsequent finaliser
// run to conflict with. (tf_close_all() is only ever called from
// engine_jni.cpp's nativeReset()/nativeDeinit(), immediately before
// mp_embed_deinit() discards the whole GC heap those Model objects lived
// on -- see tf_module.h's own comment. It must stay confined to that
// call site: calling it while the heap is still alive and a script
// might still be holding/using a Model would leave that Model's own
// interpreter/model fields dangling.)

#include <cstring>

#include <android/NeuralNetworks.h>

#include "tflite/c/c_api.h"
#include "tflite/c/c_api_experimental.h"

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"
// ulab's ndarray -- micropython_embed/ulab is already on this file's
// include path (CMakeLists.txt target_include_directories), and this
// project's own vendoring flattens ulab's upstream code/ subdirectory
// (see SESSION_STATE.yaml), so the bare "ndarray.h" spelling resolves
// directly -- no shim needed (unlike py_image.c's "ulab/code/ndarray.h",
// which needs one for OpenMV's own different expected layout).
#include "ndarray.h"
}

#include "tf_module.h"

// Forward declaration -- the real definition (extern MP_DEFINE_CONST_OBJ_TYPE(...),
// giving it external linkage, same fix as every other `const` global in
// this codebase that crosses a translation-unit or namespace boundary --
// see android_tf_version_obj's own comment below) sits AFTER the
// anonymous namespace closes, but tf_model_make_new() (inside the
// namespace) needs to reference &tf_model_type before that point in the
// token stream. Anonymous-namespace code can see file-scope names
// declared earlier by ordinary unqualified lookup -- same reasoning
// imu_module.cpp's own extern "C" const mp_obj_module_t imu_module
// (defined outside its anonymous namespace, referencing
// imu_module_globals which is defined inside it) already relies on, just
// the reverse direction.
extern const mp_obj_type_t tf_model_type;

namespace {

// Matches camera_module.cpp's/imu_module.cpp's own raise_os_error()
// shape -- duplicated per-file, same small-helper-per-module style
// already established.
void raise_os_error(int errno_, const char *msg) {
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(msg, strlen(msg)),
    };
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// Native-only registry node -- see this file's own header comment for
// why it deliberately never holds an mp_obj_t pointer. Plain malloc/
// free, not MicroPython's m_malloc/gc_alloc -- this list must stay
// valid and walkable independent of GC/interpreter state entirely
// (tf_close_all() runs right before mp_embed_deinit() tears the whole
// interpreter down).
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

// No-op if node is null (already removed/closed) or not found -- close()
// and the finaliser both call through here, and must both be safe to
// call on an already-closed Model (explicit close() then GC finalising
// the same object later, or vice versa).
void registry_remove(TfModelRegistryNode *node) {
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

struct tf_model_obj_t {
    mp_obj_base_t base;
    TfLiteInterpreter *interpreter;
    TfLiteModel *model;
    // Owned; set null once closed (idempotency marker -- interpreter/
    // model pointers above are left dangling-but-unused after close(),
    // this is the field everything actually checks).
    TfModelRegistryNode *node;
    // Phase 1 zero-copy tensor aliasing (input index 0 only -- see
    // tf_model_set_input()'s own comment for why). input_aliased is
    // monotonic once true: once a tensor is handed to LiteRT via
    // TfLiteInterpreterSetCustomAllocationForTensor, there is no public
    // API to un-alias it (confirmed by reading subgraph.cc directly --
    // custom_allocations_ is a std::map nothing in the C API clears),
    // so every later set_input() call for that index MUST also alias or
    // it raises -- silently falling back to a plain copy would write
    // through whatever buffer is STILL the tensor's backing memory,
    // which could by then be a stale/reused GC block. aliased_input_ref
    // is the GC-visible reference keeping that backing buffer's block
    // alive for as long as the interpreter might still hold a raw
    // pointer into it (i.e. for the Model's whole remaining lifetime
    // once aliased, not just for the duration of one call) -- a plain
    // mp_obj_t field inside this GC-allocated struct is enough, MicroPython's
    // GC scans the whole block, same mechanism that already makes
    // image_t._raw safe (see camera_module.cpp's image_alloc_tf_aligned()
    // comment).
    bool input_aliased;
    mp_obj_t aliased_input_ref;
};

// Shared by close() and the finaliser (__del__) -- idempotent, safe to
// call twice (e.g. explicit close() followed by the GC finalising the
// same now-unreferenced object later).
void tf_model_close_impl(tf_model_obj_t *self) {
    if (!self->node) {
        return;
    }
    registry_remove(self->node);
    self->node = nullptr;
    TfLiteInterpreterDelete(self->interpreter);
    // Model must outlive the interpreter per TfLiteInterpreterCreate's
    // own doc comment -- deleted here, after, not before.
    TfLiteModelDelete(self->model);
    self->interpreter = nullptr;
    self->model = nullptr;
    // Drop the aliased-buffer reference too -- otherwise a closed Model
    // keeps a frame buffer alive indefinitely (a real leak: 76800 bytes
    // per snapshot on a 32MB heap adds up fast, not theoretical).
    self->input_aliased = false;
    self->aliased_input_ref = mp_const_none;
}

// android.tf.Model(path) -- path is the only argument, a plain required
// positional string (VFS path, same as every other file this port
// loads -- no bundled model, see SESSION_STATE.yaml). Same
// mp_arg_parse_all_kw_array shape csi_make_new/display_make_new already
// use for make_new().
mp_obj_t tf_model_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_path };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_path, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}},
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    const char *path = mp_obj_str_get_str(parsed[ARG_path].u_obj);
    // TfLiteModelCreateFromFile is a plain libc fopen()-based C API --
    // it never goes through MicroPython's VFS layer, so a VFS-absolute
    // path like "/foo.tflite" would resolve against the real process's
    // actual OS root, not this port's VfsPosix mount. Confirmed by
    // hitting this for real on-device (OSError EINVAL, "failed to load
    // model") before finding the fix: embed_util.c's own
    // mp_embed_mount_vfs() already does a REAL os.chdir('/') syscall
    // right after mounting VfsPosix at '/' (its own comment explains
    // why -- relative paths need a real cwd to resolve against). So the
    // process's actual OS cwd already IS the VFS root -- stripping just
    // the leading '/' (not concatenating any root path) is correct and
    // sufficient, and matches how a bare relative path would already
    // behave identically to this port's own VfsPosix for the same
    // reason.
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
    // Best-effort NNAPI, silent CPU/XNNPACK fallback -- see this file's
    // own header comment. Both experimental C API, subject to change,
    // same standing caveat as TfLiteInterpreterSetCustomAllocationForTensor
    // (SESSION_STATE.yaml).
    TfLiteInterpreterOptionsSetUseNNAPI(options, true);
    TfLiteInterpreterOptionsSetEnableDelegateFallback(options, true);

    TfLiteInterpreter *interpreter = TfLiteInterpreterCreate(model, options);
    // Safe to delete options immediately after TfLiteInterpreterCreate
    // returns -- the interpreter doesn't retain it (c_api.h's own doc
    // comment on TfLiteInterpreterCreate, confirmed by reading it, not
    // assumed).
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

    // mp_obj_malloc_with_finaliser (MICROPY_ENABLE_FINALISER, confirmed
    // ON) -- this file's own header comment explains why: the GC-safe
    // way to guarantee close()-equivalent cleanup runs if a script
    // drops a Model without calling close() itself, without the
    // registry ever needing to hold a pointer INTO the GC heap.
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

// set_input(data, index=0) -- data must match the input tensor's exact
// byte size (TfLiteTensorByteSize), checked explicitly before either
// path below -- a deliberate boundary check, not redundant (advisor
// review flagged this specifically: script-supplied bytes reaching a
// native memcpy-shaped call without a length check first is a real
// overflow risk). Assumes a static-shaped model -- TfLiteTensorByteSize
// is read once, before any AllocateTensors() call the alias path below
// might make; a genuinely dynamic-shape model could in principle make
// that read stale, not handled here.
//
// Phase 1 zero-copy tensor aliasing (see SESSION_STATE.yaml's design
// discussion, and camera_module.cpp's image_alloc_tf_aligned() --
// prerequisite work making every csi.snapshot() result 64-byte
// aligned): attempted ONLY for index 0 (the common single-input-model
// case -- a model with multiple inputs would need independent per-index
// alias state this struct doesn't track; a deliberate scope limit, not
// an oversight) and ONLY via TfLiteInterpreterSetCustomAllocationForTensor,
// which needs a real TENSOR index (TfLiteInterpreterGetInputTensorIndex),
// not the input-relative index set_input() itself takes.
//
// MP_BUFFER_READ, not RW -- not a shortcut. c_api_experimental.h's own
// doc comment on TfLiteInterpreterSetCustomAllocationForTensor says
// read-only is the CORRECT permission for input tensors ("Read-only for
// inputs, Read-Write for others"), not just what happens to be
// convenient. It matters concretely too: py_image_obj_t (camera_
// module.cpp/vendored py_image.c) -- the actual motivating use case for
// this whole feature -- hard-rejects any non-read buffer request
// ("Can't write to an image!", pristine vendored code, confirmed by
// reading it directly). Gating on MP_BUFFER_RW instead, as an earlier
// design pass considered, would have silently excluded the one buffer
// type this was built for.
//
// Monotonic once aliased (see tf_model_obj_t's own comment for why: no
// public API un-aliases a tensor once SetCustomAllocationForTensor
// succeeds on it) -- a later set_input() call for the same (aliased)
// index that can't also alias RAISES rather than silently falling back
// to a copy, which would write through whatever buffer is still the
// tensor's backing memory (possibly a stale, already-reclaimed GC block
// by then). The fix for that error is exactly what the message says:
// construct a new Model().
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
        // Ordinary case: not aliased, buffer doesn't qualify -- plain copy.
        if (TfLiteTensorCopyFromBuffer(tensor, bufinfo.buf, bufinfo.len) != kTfLiteOk) {
            raise_os_error(MP_EIO, "android.tf: failed to copy input data into tensor");
        }
        return mp_const_none;
    }

    // Aligned -- attempt the zero-copy path (either entering alias mode
    // for the first time, or maintaining it). Store the GC reference
    // BEFORE the call, not after: if SetCustomAllocationForTensor
    // succeeds but the AllocateTensors() re-plan below fails, the tensor
    // is already kTfLiteCustom pointing at this buffer regardless -- the
    // reference must already be rooted at that point, not only once the
    // whole sequence is known to have succeeded.
    self->aliased_input_ref = args[ARG_data].u_obj;

    int32_t tensor_index = TfLiteInterpreterGetInputTensorIndex(self->interpreter, index);
    TfLiteCustomAllocation allocation = {(void *) bufinfo.buf, bufinfo.len};
    if (TfLiteInterpreterSetCustomAllocationForTensor(self->interpreter, tensor_index, &allocation, kTfLiteCustomAllocationFlagsNone) != kTfLiteOk) {
        // Tensor untouched -- the real C++ implementation ENSUREs
        // (allocation_type, alignment) before committing anything to
        // custom_allocations_ (confirmed by reading subgraph.cc
        // directly, not assumed) -- NOT in alias mode, safe to fall
        // back to a plain copy this one time. self->input_aliased is
        // guaranteed false here (the raise above already handled the
        // "was aliased, can't alias again" case), so there is nothing
        // to preserve.
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
        // The tensor IS kTfLiteCustom now regardless (the call above
        // already committed it) -- this model is broken, don't try to
        // recover or fall back to copying.
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

// get_output(index=0) -- returns a real copy (mp_obj_new_bytes(), which
// copies internally), never a view into the tensor arena: that arena is
// reused/overwritten by the next invoke() and freed on close(), so a
// zero-copy view here would be a live footgun (advisor review flagged
// this specifically too) -- correctness over the small copy cost, same
// tier of tradeoff as this project's other raw-tensor-only v1 scoping
// decisions.
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

// TfLiteType -> ulab NDARRAY_* dtype char, for set_input_ndarray()/
// get_output_ndarray() below -- deliberately narrower than
// tf_dtype_name()'s full 24-value mapping: ulab only HAS four numeric
// dtypes (NDARRAY_UINT8/INT8/UINT16/INT16/FLOAT), so int32/int64/bool/
// string/etc tensors have no ulab representation at all, not a gap
// worth working around. -1 (not a valid uint8_t dtype value) signals
// unsupported.
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

// set_input_ndarray(data, index=0) -- ulab-ndarray-typed sibling of
// set_input(), matching OpenMV's py_ml_process_input() conversion
// (github.com/openmv/openmv modules/py_ml.c, read directly this
// session): quantizes real_value -> raw per element
// (raw = value * (1/scale) + zero_point, cast to the tensor's dtype)
// for int8/uint8/int16/uint16 tensors, a plain per-element cast for
// float32. A SEPARATE method, not a type-check inside set_input()
// itself (user's own call) -- keeps set_input()'s existing plain-copy/
// zero-copy-alias logic and invariants completely untouched; this
// method always writes a freshly-converted value, never aliases, and
// works at any index (unlike the 64-byte-alignment zero-copy path,
// which is index-0-only).
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

    // Can't satisfy the zero-copy-alias monotonic invariant (set_input()'s
    // own comment) with a converted write -- the tensor's backing memory
    // would no longer BE this ndarray's buffer, just values copied from it.
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
        TfLiteQuantizationParams quant = TfLiteTensorQuantizationParams(tensor);
        float inv_scale = quant.scale != 0.0f ? (1.0f / quant.scale) : 1.0f;
        for (size_t i = 0; i < len; i++) {
            float v = (float) ndarray_get_float_index(src->array, src->dtype, i);
            float raw = v * inv_scale + quant.zero_point;
            switch (tensor_type) {
                case kTfLiteInt8: ((int8_t *) dst)[i] = (int8_t) raw; break;
                case kTfLiteUInt8: ((uint8_t *) dst)[i] = (uint8_t) raw; break;
                case kTfLiteInt16: ((int16_t *) dst)[i] = (int16_t) raw; break;
                case kTfLiteUInt16: ((uint16_t *) dst)[i] = (uint16_t) raw; break;
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

// get_output_ndarray(index=0) -- ulab-ndarray-typed sibling of
// get_output(), matching OpenMV's py_ml_process_output() dequantize
// behavior (deep_copy=True path -- always a fresh, safe copy, same
// reasoning as get_output()'s own "never a view into the tensor arena"
// comment, no zero-copy option offered here either): int8/uint8/int16/
// uint16 tensors auto-dequantize to a float ndarray
// (real = (raw - zero_point) * scale, OpenMV's own formula) -- user's
// own call, matching OpenMV's actual behavior rather than this
// project's usual raw-facts-only default, since scale/zero_point alone
// (model.info()) already covers the "give me the raw facts" case.
// float32 tensors copy straight across -- ulab's NDARRAY_FLOAT is
// FLOAT_TYPECODE, which IS mp_float_t/float now that MICROPY_FLOAT_IMPL
// is FLOAT (see mpconfigport.h), byte-identical to TfLite's float32, so
// this is a real memcpy, not a per-element convert loop.
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

    // Quantized -- always dequantize to a float ndarray (OpenMV-style,
    // user's own call), never a raw-typed ndarray of the quantized
    // dtype -- see this function's own header comment.
    TfLiteQuantizationParams quant = TfLiteTensorQuantizationParams(tensor);
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

// TfLiteType -> canonical dtype name string. NOT an invented label like
// the camera module's deliberately-raw facing ints (see
// SESSION_STATE.yaml's android.tf design discussion for why that
// distinction matters) -- this is a mechanical, canonical mapping (the
// same names numpy/every ML tool already uses for these types), not an
// opinionated interpretation. Pulled from the real
// tflite/converter/core/c/tflite_types.h enum directly (24 values,
// confirmed by reading it, not guessed). Anything outside that set
// (a future LiteRT type this pinned header doesn't know about yet)
// falls back to "unknown(<int>)" rather than mislabeling it or raising
// -- info() is a diagnostic call, it shouldn't fail a script over an
// unrecognized type it can still report the raw value for.
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

// One dict per tensor: {'name':..., 'shape':[...], 'dtype':..., 'bytes':...,
// 'scale':..., 'zero_point':...}.
// Real gap this closes (see SESSION_STATE.yaml): writing
// examples/tf_selftest/tf_selftest.py needed a whole separate
// standalone NDK probe just to discover add_simple.tflite's input
// shape/dtype/byte-size -- nothing let a script ask a loaded Model this
// directly. shape is a plain list, not a tuple -- avoids a fixed-size
// stack buffer/VLA for an unbounded dimension count, and matches the
// same mp_obj_new_list()/mp_obj_list_append() idiom already used for
// inputs/outputs below and camera_list() in camera_module.cpp, not a
// new pattern.
//
// scale/zero_point: without these, get_output()'s raw bytes are
// uninterpretable for any int8/uint8-quantized model (the norm for
// on-device inference, not the exception -- add_simple.tflite, this
// project's only test fixture so far, happens to be float32, which is
// why this gap wasn't caught by tf_selftest.py). Real value is
// `scale * (quantized_value - zero_point)` (TfLiteTensorQuantizationParams's
// own doc comment, c_api.h). Raw pass-through, same philosophy as
// dtype/shape above -- not an opinionated "is this quantized" bool.
// scale == 0.0 is the C API's own documented signal that the tensor
// isn't (legacy-style, per-tensor) quantized, so a script can check
// that itself rather than this dict inventing a redundant flag.
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

// model.info() -- {'inputs': [...], 'outputs': [...], 'input_aliased':
// bool}, one dict per tensor from tf_tensor_info_dict() above.
// input_aliased reports whether the LAST set_input() call took the
// Phase 1 zero-copy alias path (see tf_model_set_input()'s own
// comment) -- True iff self->input_aliased, which starts False at
// construction, so a fresh Model with no set_input() call yet correctly
// reports False rather than being absent or stale.
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

// __del__ -- the finaliser itself (see this file's own header comment).
// Only reached by py/gc.c's finaliser sweep on a Model the GC is about
// to actually free -- never called directly by a script. Same
// idempotent close logic as explicit close(), so a script that DOES
// call close() itself just makes this a no-op later.
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

// android.tf.info()'s hw_nnapi field -- confirmed to need a SEPARATE
// native surface from LiteRT entirely (Android's own NNAPI device-
// enumeration API, <android/NeuralNetworks.h>, libneuralnetworks.so --
// not something LiteRT's own C API exposes at all; confirmed earlier
// via nm -D that TfLiteInterpreterOptionsSetUseNNAPI is the ONLY
// NNAPI-related symbol LiteRT itself exports). Deliberately checks
// each enumerated device's real TYPE, not just count() > 0 -- every
// Android device enumerates at least one CPU-fallback pseudo-device
// (ANEURALNETWORKS_DEVICE_CPU) regardless of real accelerator
// hardware, so count>0 alone would always report True and mean
// nothing. True iff at least one device is GPU or ACCELERATOR (i.e.
// genuinely not the trivial always-present CPU device) -- a device
// CAPABILITY fact, not proof any given model actually dispatches to
// it (LiteRT's own NNAPI usage is unqueryable after the fact -- see
// this file's own "nnapi" field, dropped entirely for exactly that
// reason; hw_nnapi deliberately answers a different, answerable
// question: "could this device ever accelerate", not "did this model
// get accelerated").
//
// Direct link (target_link_libraries(upy_engine ... neuralnetworks) in
// CMakeLists.txt), not dlopen/dlsym -- libneuralnetworks.so itself
// needs API 27 (checked ANeuralNetworksModel_create's own
// __NNAPI_INTRODUCED_IN annotation directly, not assumed), and this
// project's minSdk was bumped 26->27 specifically to make that a safe,
// eager DT_NEEDED dependency (see build.gradle.kts's own comment) --
// the library is now guaranteed present on every device this app can
// even install on. The device-enumeration functions used below are
// still API 29-only, though (confirmed the same way) -- minSdk bumping
// to 27 doesn't change that, so a real availability guard is still
// required; this is the standard Android NDK pattern for a symbol
// above minSdk but below the library's own baseline (direct link + a
// guarded call, not dlopen/dlsym, since Android's lazy PLT binding only
// resolves a called function at the point it's actually invoked, not
// at library load time). __builtin_available (below), not a plain
// android_get_device_api_level() runtime check -- see the function
// body's own comment for why a plain runtime check isn't enough here.
bool tf_hw_nnapi_available() {
    // __builtin_available, not a plain android_get_device_api_level()
    // runtime check -- clang's own availability enforcement for a
    // symbol whose __NNAPI_INTRODUCED_IN exceeds the compile target
    // (android27 here) is a genuine COMPILE-TIME error ("... is
    // unavailable: introduced in Android 29"), confirmed by hitting it
    // for real, not a warning a plain runtime guard would satisfy --
    // clang only recognizes this specific construct as proof the call
    // below can't execute pre-29, the same mechanism Objective-C uses
    // for the same purpose.
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

// extern prefix required -- MP_DEFINE_CONST_OBJ_TYPE expands to a plain
// `const mp_obj_type_t tf_model_type = {...}`, which in C++ has
// INTERNAL linkage by default even here at file scope (same rule
// already hit twice this session for MP_DEFINE_CONST_FUN_OBJ_N objects
// in imu_module.cpp/camera_module.cpp -- extern turns this into a real
// external-linkage definition android_module.cpp can reference).
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

// android.tf.info() -- replaces this file's original version()-only
// mockup outright (user's own call: "replace outright, cleaner" --
// see SESSION_STATE.yaml's android.tf design discussion). {'version':
// str, 'hw_nnapi': bool} -- hw_nnapi via tf_hw_nnapi_available() above
// (Android's own NNAPI device-enumeration NDK API, a different surface
// than LiteRT entirely). Declared outside the anonymous namespace above
// (unlike everything else in this file) so android_module.cpp can
// reference the function object by extern -- `extern` prefix required,
// same C++-internal-linkage-by-default reasoning as
// android_proximity_distance_cm_obj/android_light_on_obj/
// android_zoom_set_obj.
mp_obj_t android_tf_info() {
    mp_obj_t dict = mp_obj_new_dict(2);
    const char *version = TfLiteVersion();
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_version), mp_obj_new_str(version, strlen(version)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_hw_nnapi), tf_hw_nnapi_available() ? mp_const_true : mp_const_false);
    return dict;
}
extern MP_DEFINE_CONST_FUN_OBJ_0(android_tf_info_obj, android_tf_info);
