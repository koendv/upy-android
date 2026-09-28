// upy-android OpenMV path shim, not a stub. Real upstream ulab has
// ndarray.h under a code/ subdirectory. This project's existing
// vendoring (native-bringup/vendor/ulab/) deliberately flattens that:
// code/'s contents are copied directly into vendor/ulab/, no code/
// subdirectory, so py_image.c's own "ulab/code/ndarray.h" include
// doesn't resolve as-is against this tree. Rather than reshaping the
// real, already-verified ulab vendoring to match OpenMV's expectation
// (or vice versa), this one-line passthrough bridges the two layouts.
#include "ulab/ndarray.h"
