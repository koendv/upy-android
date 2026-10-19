# test: required
#
# gif.help() and the statements in it that a script can check.
import os

import gif
import image


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


PATH = "/gif_help_test.gif"
remove(PATH)
text = gif.help()
print("help:", text.startswith("module: gif"))
print("help names the documentation:", "https://docs.openmv.io/" in text)

g = gif.Gif(PATH)
print("size without a camera is 320x240 grayscale:", (g.width(), g.height(), g.format()) == (320, 240, image.GRAYSCALE))
print("loop is 1 by default:", g.loop() == 1)
print("wrong geometry raises OSError:", raises(OSError, lambda: g.add_frame(image.Image(10, 10, image.GRAYSCALE))))
print("BINARY raises OSError:", raises(OSError, lambda: g.add_frame(image.Image(320, 240, image.BINARY))))
g.close()
remove(PATH)

g = gif.Gif(PATH, width=64, height=48, color=True, loop=False)
print("explicit size:", (g.width(), g.height(), g.format(), g.loop()) == (64, 48, image.RGB565, 0))
g.add_frame(image.Image(64, 48, image.RGB565))
print("size grows:", g.size() > 0)
g.close()
remove(PATH)
