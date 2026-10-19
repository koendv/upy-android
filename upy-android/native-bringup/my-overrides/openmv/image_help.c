// image.help(): what differs from OpenMV's image module. Added to the module
// by patches/openmv/py_image.patch.
#include <string.h>
#include "py/obj.h"
#include "py/runtime.h"

static const char image_help_text[] =
    "module: image (OpenMV's image module)\n"
    "documentation: https://docs.openmv.io/v5.0.1/library/omv.image.html\n"
    "  methods and arguments are OpenMV's. This text lists what differs; that page is for v5.0.1, so details can differ.\n"
    "methods: dir(image.Image); arguments: the documentation above (functions here have no docstrings)\n"
    "images come from camera.Camera().snapshot(), image.Image(path), image.Image(w, h, format) or image.Image(ndarray)\n"
    "also available here:\n"
    "  gamma_corr(), gamma(), awb(), ccm(): awb() and ccm() change RGB565 images only; a grayscale image is left unchanged\n"
    "  selective_search()\n"
    "  stereo_disparity(): grayscale image of even width, else ValueError\n"
    "  ImageIO\n"
    "  to_ndarray(dtype): dtype 'b', 'B' or 'f'; shape (h, w) for grayscale, (h, w, 3) for RGB565\n"
    "  image.Image(ndarray): float ndarray of shape (h, w) or (h, w, 3), else ValueError\n"
    "differences from OpenMV:\n"
    "  no frame buffer: copy_to_fb=True raises OSError (No free buffers!); leave copy_to_fb out\n"
    "  ImageIO.read(): copy_to_fb defaults to False (OpenMV: True)\n"
    "  flush() does nothing\n"
    "  find_keypoints(): OpenMV's default detector, AGAST, is not included (GPL) and the default returns None; use corner_detector=image.CORNER_FAST\n"
    "  AprilTag families: image.TAG16H5, TAG25H9, TAG36H10 and TAG36H11 only; the other TAG* constants do not exist\n"
    "  not provided, they need a ToF or event camera: to_depth(), to_evt_dark(), to_evt_light(), draw_event_histogram(), PALETTE_DEPTH, PALETTE_EVT_DARK, PALETTE_EVT_LIGHT\n"
    "  csi, sensor, omv and ml do not exist (ImportError): use camera, tflite and litert\n"
    "haar cascades are in /rom, as in OpenMV: image.HaarCascade(\"/rom/haarcascade_frontalface.cascade\", stages=25), \"/rom/haarcascade_eye.cascade\" with stages=24\n"
    "see_also: camera.help(), display.help()\n"
;

mp_obj_t image_help(void) {
    return mp_obj_new_str(image_help_text, strlen(image_help_text));
}
MP_DEFINE_CONST_FUN_OBJ_0(image_help_obj, image_help);
