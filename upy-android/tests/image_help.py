# test: required
#
# image.help() and the statements in it that a script can check.
import image


def raises(exception, call):
    try:
        call()
    except exception:
        return True
    return False


def importable(name):
    try:
        __import__(name)
    except ImportError:
        return False
    return True


text = image.help()
print("help:", text.startswith("module: image"))
print("help names the documentation:", "https://docs.openmv.io/" in text)
print("methods listed by dir:", all(n in dir(image.Image) for n in ("find_blobs", "gamma_corr", "to_ndarray")))

print("copy_to_fb=True raises OSError:", raises(OSError, lambda: image.Image(8, 8, image.GRAYSCALE, copy_to_fb=True)))
print("flush does nothing:", image.Image(8, 8, image.GRAYSCALE).flush() is None)

W, H, SQ = 320, 240, 20
img = image.Image(W, H, image.GRAYSCALE)
img.draw_rectangle((0, 0, W, H), color=255, fill=True)
for y in range(0, H, SQ):
    for x in range(0, W, SQ):
        if (x // SQ + y // SQ) % 2:
            img.draw_rectangle((x, y, SQ, SQ), color=0, fill=True)
print("find_keypoints default returns None:", img.find_keypoints() is None)
print("find_keypoints with CORNER_FAST:", img.find_keypoints(corner_detector=image.CORNER_FAST) is not None)

print("AprilTag families:", [n for n in ("TAG16H5", "TAG25H9", "TAG36H10", "TAG36H11") if hasattr(image, n)] == ["TAG16H5", "TAG25H9", "TAG36H10", "TAG36H11"])
print("other AprilTag families absent:", not any(hasattr(image, n) for n in ("TAGCIRCLE21H7", "TAGCIRCLE49H12", "TAGCUSTOM48H12", "TAGSTANDARD41H12", "TAGSTANDARD52H13")))
print("hardware functions absent:", not any(hasattr(image.Image, n) for n in ("to_depth", "to_evt_dark", "to_evt_light", "draw_event_histogram")))
print("hardware constants absent:", not any(hasattr(image, n) for n in ("PALETTE_DEPTH", "PALETTE_EVT_DARK", "PALETTE_EVT_LIGHT")))
print("csi, sensor, omv, ml absent:", not any(importable(n) for n in ("csi", "sensor", "omv", "ml")))

image.HaarCascade("/rom/haarcascade_frontalface.cascade", stages=25)
image.HaarCascade("/rom/haarcascade_eye.cascade", stages=24)
print("haar cascades load from /rom: True")

gray = image.Image(8, 8, image.GRAYSCALE)
gray.draw_rectangle((0, 0, 4, 8), color=200, fill=True)
gray.draw_rectangle((4, 0, 4, 8), color=50, fill=True)
gray.awb()
print("awb leaves grayscale alone:", (gray.get_pixel((0, 0)), gray.get_pixel((7, 0))) == (200, 50))
print("stereo_disparity needs grayscale:", raises(ValueError, lambda: image.Image(64, 64, image.RGB565).stereo_disparity()))
print("stereo_disparity needs even width:", raises(ValueError, lambda: image.Image(63, 64, image.GRAYSCALE).stereo_disparity()))
