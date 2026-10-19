// ulab.help(). Added to the module by patches/ulab/ulab.patch. No version
// number in the text, so it does not change with a new ulab.
#include <string.h>
#include "py/obj.h"
#include "py/runtime.h"

static const char ulab_help_text[] =
    "module: ulab (micropython-ulab, numpy and scipy for MicroPython)\n"
    "documentation: https://micropython-ulab.readthedocs.io/en/latest/ (latest version)\n"
    "  this version: print(ulab.__version__), '<release>-(<dims>)D' or '<release>-(<dims>)D-c'\n"
    "  <release> is the ulab release; its manual is the docs folder of the git tag with that name:\n"
    "  https://github.com/v923z/micropython-ulab/tree/<release>/docs\n"
    "  <dims> is the largest number of array dimensions (ulab's default is 2; more raises TypeError); -c: complex numbers\n"
    "  functions and arguments are ulab's. This text lists how it is set up here.\n"
    "import: from ulab import numpy as np; also ulab.scipy and ulab.utils. import numpy and import ulab.numpy do not work\n"
    "contents: numpy (with fft, linalg and random), scipy (integrate, linalg, optimize, signal, special), utils\n"
    "images: image.to_ndarray(dtype) and image.Image(ndarray) convert between an image and an array, see image.help()\n"
    "see_also: image.help()\n"
;

mp_obj_t ulab_help(void) {
    return mp_obj_new_str(ulab_help_text, strlen(ulab_help_text));
}
MP_DEFINE_CONST_FUN_OBJ_0(ulab_help_obj, ulab_help);
