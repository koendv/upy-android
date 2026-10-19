# test: required
#
# mjpeg.help() and the statements in it that a script can check.
import os

import image
import mjpeg


def raises(exception, call):
    try:
        call()
    except exception:
        return True
    return False


def remove(path):
    try:
        os.remove(path)
    except OSError:
        pass


PATH = "/mjpeg_help_test.mjpeg"
remove(PATH)
text = mjpeg.help()
print("help:", text.startswith("module: mjpeg"))
print("help names the documentation:", "https://docs.openmv.io/" in text)

img = image.Image(320, 240, image.GRAYSCALE)
m = mjpeg.Mjpeg(PATH)
print("size without a camera is 320x240:", (m.width(), m.height()) == (320, 240))
m.add_frame(img)
m.write(img, quality=50)
print("add_frame and write count frames:", m.count() == 2 and m.size() > 0)
print("quality out of range raises ValueError:", raises(ValueError, lambda: m.add_frame(img, quality=101)))
print("alpha out of range raises ValueError:", raises(ValueError, lambda: m.add_frame(img, alpha=256)))
print("rgb_channel out of range raises ValueError:", raises(ValueError, lambda: m.add_frame(img, rgb_channel=3)))
m.sync()
print("is_closed is 0 before close:", m.is_closed() == 0)
m.close()
print("is_closed is 1 after close:", m.is_closed() == 1)
print("add_frame after close raises OSError:", raises(OSError, lambda: m.add_frame(img)))
print("sync after close raises OSError:", raises(OSError, lambda: m.sync()))
remove(PATH)

m = mjpeg.Mjpeg(PATH, 64, 48)
print("explicit size:", (m.width(), m.height()) == (64, 48))
m.close()
remove(PATH)
