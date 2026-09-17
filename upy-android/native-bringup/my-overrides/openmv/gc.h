// upy-android OpenMV support layer -- shim, not a real header. fast.c
// (vendored as-is from upstream) does `#include "gc.h"` (unqualified),
// expecting to find it on its own compile unit's include path the way
// a real OpenMV board's build sets up -- our own -I set only reaches
// MicroPython's real header at "py/gc.h" (via -Imicropython_embed).
// Same pattern as ulab-shim/ulab/code/ndarray.h: a one-line passthrough
// rather than reshaping either side's layout.
#include "py/gc.h"
