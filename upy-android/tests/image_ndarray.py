# test: required
#
# image.to_ndarray() and image.Image(ndarray) with a ulab ndarray, which
# OpenMV's own unit tests do not cover. Pixel values are 8 bit; 'b' is the
# value minus 128.
import image
from ulab import numpy as np


def gray():
    img = image.Image(4, 3, image.GRAYSCALE)
    for y in range(3):
        for x in range(4):
            img.set_pixel((x, y), y * 40 + x * 10)
    return img


def rgb():
    img = image.Image(2, 2, image.RGB565)
    img.draw_rectangle((0, 0, 1, 2), color=(200, 100, 50), fill=True)
    img.draw_rectangle((1, 0, 1, 2), color=(10, 250, 128), fill=True)
    return img


def pixels(img):
    return [img.get_pixel((x, y)) for y in range(img.height()) for x in range(img.width())]


def raises(exception, call):
    try:
        call()
    except exception:
        return True
    return False


g = gray()
print("grayscale shape:", g.to_ndarray("B").shape == (3, 4))
print("grayscale B values:", g.to_ndarray("B").tolist() == [[0, 10, 20, 30], [40, 50, 60, 70], [80, 90, 100, 110]])
print("grayscale f values:", g.to_ndarray("f").tolist()[1] == [40.0, 50.0, 60.0, 70.0])
print("grayscale b values:", g.to_ndarray("b").tolist()[0] == [-128, -118, -108, -98])

c = rgb()
print("RGB565 shape:", c.to_ndarray("f").shape == (2, 2, 3))
print("RGB565 values:", c.to_ndarray("f").tolist()[0] == [list(c.get_pixel((0, 0), rgbtuple=True)), list(c.get_pixel((1, 0), rgbtuple=True))])

back = image.Image(g.to_ndarray("f"))
print("grayscale round trip:", back.format() == image.GRAYSCALE and (back.width(), back.height()) == (4, 3) and pixels(back) == pixels(g))
back = image.Image(c.to_ndarray("f"))
print("RGB565 round trip:", back.format() == image.RGB565 and (back.width(), back.height()) == (2, 2) and pixels(back) == pixels(c))

buf = bytearray(12)
g.to_ndarray("B", buffer=buf)
print("buffer is filled:", buf[:4] == bytes([0, 10, 20, 30]))
print("buffer too small:", raises(ValueError, lambda: g.to_ndarray("B", buffer=bytearray(3))))
print("dtype not supported:", raises(ValueError, lambda: g.to_ndarray("h")))
print("Image needs a float array:", raises(ValueError, lambda: image.Image(np.array([[1, 2], [3, 4]], dtype=np.uint8))))
print("Image needs 2 or 3 dimensions:", raises(ValueError, lambda: image.Image(np.array([1.0, 2.0, 3.0], dtype=np.float))))
