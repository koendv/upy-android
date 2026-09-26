// upy-android tflm_backend.cc path shim, not a stub -- same reasoning
// as ulab-shim/ulab/code/ndarray.h (see that file's own comment):
// tflm_backend.cc's own #include "common/omv_profiler.h" (its real
// upstream path) doesn't resolve as-is against this project's
// flattened openmv/ tree (my-overrides/openmv/omv_profiler.h, no
// common/ subdirectory). One-line passthrough bridges the two layouts,
// scoped only to tflm_backend.cc's own compile (see CMakeLists.txt).
//
// A real relative path, not "omv_profiler.h" -- this shim file's own
// name is ALSO "omv_profiler.h", so a quoted #include "omv_profiler.h"
// here would find itself first (quoted-include search checks the
// including file's own directory before any -I path) and recurse
// infinitely ("#include nested too deeply", caught by an actual
// compile attempt, not spotted by inspection). "../../" reaches
// my-overrides/openmv/ (this shim's real target) directly, bypassing
// the -I search order entirely.
#include "../../omv_profiler.h"
