# test: required
#
# image: gamma_corr(), ccm(), awb(), and ImageIO.read() with its default
# copy_to_fb=False. OpenMV's own unit tests cover only gamma_corr() and
# ImageIO with an explicit copy_to_fb=False.
import os

import image

PATH = "/image_isp_test.bin"


def rgb(color):
    img = image.Image(8, 8, image.RGB565)
    img.draw_rectangle((0, 0, 8, 8), color=color, fill=True)
    return img


def pixel(img):
    return img.get_pixel((0, 0), rgbtuple=True)


def gray(value):
    img = image.Image(8, 8, image.GRAYSCALE)
    img.draw_rectangle((0, 0, 8, 8), color=value, fill=True)
    return img


# gamma_corr
img = gray(128)
print("gamma_corr returns the image:", img.gamma_corr(gamma=0.5) is img)
print("gamma 0.5 darkens:", img.get_pixel((0, 0)) < 128)
img = gray(128)
img.gamma_corr(gamma=2.0)
print("gamma 2.0 brightens:", img.get_pixel((0, 0)) > 128)
img = gray(128)
img.gamma_corr(gamma=1.0)
print("gamma 1.0 changes at most 1 level:", abs(img.get_pixel((0, 0)) - 128) <= 1)
img = rgb((100, 100, 100))
img.gamma_corr(gamma=2.0)
print("gamma 2.0 brightens RGB565:", all(c > 120 for c in pixel(img)))

# ccm
r, g, b = pixel(rgb((200, 100, 50)))
img = rgb((200, 100, 50))
print("ccm returns the image:", img.ccm([1, 0, 0, 0, 1, 0, 0, 0, 1]) is img)
print("ccm identity:", pixel(img) == (r, g, b))
img = rgb((200, 100, 50))
img.ccm([[0, 0, 1], [0, 1, 0], [1, 0, 0]])
print("ccm swaps red and blue:", pixel(img) == (b, g, r))
img = rgb((200, 100, 50))
img.ccm([2, 0, 0, 0, 2, 0, 0, 0, 2])
print("ccm doubles and clips:", pixel(img)[0] == 255 and pixel(img)[1] > g)
img = rgb((100, 100, 100))
r, g, b = pixel(img)
img.ccm([1, 0, 0, 8, 0, 1, 0, 0, 0, 0, 1, 0])
print("ccm offset raises red only:", pixel(img)[0] > r and pixel(img)[1:] == (g, b))
img = gray(100)
img.ccm([2, 0, 0, 0, 2, 0, 0, 0, 2])
print("ccm leaves grayscale alone:", img.get_pixel((0, 0)) == 100)
try:
    rgb((1, 2, 3)).ccm([1, 2, 3, 4, 5])
    print("ccm bad size raises ValueError: False")
except ValueError:
    print("ccm bad size raises ValueError: True")

# awb
for name, kwargs in (("gray world", {}), ("white patch", {"max": True})):
    img = rgb((200, 100, 50))
    print("awb %s returns the image:" % name, img.awb(**kwargs) is img)
    p = pixel(img)
    print("awb %s makes a color patch neutral:" % name, max(p) - min(p) <= 8)

# ImageIO.read() without copy_to_fb
img = rgb((200, 100, 50))
stream = image.ImageIO((8, 8, image.RGB565), 2)
stream.write(img)
stream.write(img)
stream.seek(0)
back = stream.read()
print("memory stream read:", back.width() == 8 and back.height() == 8 and pixel(back) == pixel(img))
try:
    os.remove(PATH)
except OSError:
    pass
stream = image.ImageIO(PATH, "w")
stream.write(img)
stream.close()
stream = image.ImageIO(PATH, "r")
back = stream.read()
stream.close()
os.remove(PATH)
print("file stream read:", back.width() == 8 and back.height() == 8 and pixel(back) == pixel(img))
