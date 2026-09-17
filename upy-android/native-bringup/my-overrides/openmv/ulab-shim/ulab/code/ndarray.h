// upy-android OpenMV compile-only spike -- path shim, not a stub. Real
// upstream ulab has ndarray.h under a code/ subdirectory; this project's
// existing vendoring (native-bringup/my-overrides/ulab/, see
// SESSION_STATE.yaml's FETCH.sh) deliberately FLATTENS that -- code/'s
// contents are copied directly into my-overrides/ulab/, no code/
// subdirectory -- so py_image.c's own "ulab/code/ndarray.h" include
// doesn't resolve as-is against our tree. Rather than reshaping the real,
// already-verified ulab vendoring to match OpenMV's expectation (or vice
// versa), this one-line passthrough bridges the two layouts.
#include "ulab/ndarray.h"
