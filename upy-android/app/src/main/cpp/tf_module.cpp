// upy-android native tf module -- MOCKUP / proof-of-concept for
// android.tf (see SESSION_STATE.yaml's "android.tf" design discussion).
// Exists to prove the LiteRT CMake wiring (litert/include,
// litert/lib/libLiteRt.so, see CMakeLists.txt's own comment) actually
// links and runs a real LiteRT C API call on-device, reached from a
// MicroPython script over the same AIDL exec() path every other module
// uses -- NOT the real android.tf API surface. The real design (raw
// output tensors, model loading from the VFS, tensor-input aliasing via
// TfLiteInterpreterSetCustomAllocationForTensor) is still future work.
//
// One function only: android.tf.version() -- returns LiteRT's own
// TfLiteVersion() C API string. TfLiteVersion (and
// TfLiteInterpreterSetCustomAllocationForTensor, for the real work
// later) were confirmed present and exported via `nm -D` on the real
// libLiteRt.so, and confirmed callable via a standalone on-device probe,
// before any of this was wired into the actual project -- see
// SESSION_STATE.yaml.

#include <cstring>

#include "tflite/c/c_api.h"

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
}

// Declared outside any anonymous namespace, `extern` prefix required --
// same C++-internal-linkage-by-default reasoning as
// android_proximity_distance_cm_obj/android_light_on_obj/
// android_zoom_set_obj (imu_module.cpp/camera_module.cpp's own
// comments): a `const` global has internal linkage by default in C++,
// so android_module.cpp couldn't see this at link time without it.
mp_obj_t android_tf_version() {
    const char *version = TfLiteVersion();
    return mp_obj_new_str(version, strlen(version));
}
extern MP_DEFINE_CONST_FUN_OBJ_0(android_tf_version_obj, android_tf_version);
