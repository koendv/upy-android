# find_displacement.py
#
# - in Files, tap find_displacement.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - csi0.framesize() picks this device's smallest supported resolution; a 64x64 patch from the frame center replaces the 64x64 camera mode
# - the previous patch is kept as an image instead of csi0.width()/height()/pixformat(), not implemented on Android
# - frames are shown on the Camera tab via display.SPIDisplay
#
# This work is licensed under the MIT license.
# Copyright (c) 2013-2023 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# Differential Optical Flow Translation
#
# This example shows off using your OpenMV Cam to measure translation
# in the X and Y direction by comparing the current and the previous
# image against each other. Note that only X and Y translation is
# handled - not rotation/scale in this mode.
#
# To run this demo effectively please mount your OpenMV Cam on a steady
# base and QUICKLY translate it to the left, right, up, and down and
# watch the numbers change. Note that you can see displacement numbers
# up +- half of the hoizontal and vertical resolution.
#
# NOTE You have to use a small power of 2 resolution when using
# find_displacement(). This is because the algorithm is powered by
# something called phase correlation which does the image comparison
# using FFTs. A non-power of 2 resolution requires padding to a power
# of 2 which reduces the usefulness of the algorithm results. Please
# use a resolution like B64X64 or B64X32 (2x faster).
#
# Your OpenMV Cam supports power of 2 resolutions of 64x32, 64x64,
# 128x64, and 128x128. If you want a resolution of 32x32 you can create
# it by doing "img.scale(x_scale=0.5, y_scale=0.5, hint=image.AREA)" on a 64x64 image.

import csi
import image
import time
import display

csi0 = csi.CSI()
csi0.reset()  # Reset and initialize the sensor.
csi0.pixformat(csi.RGB565)  # Set pixel format to RGB565 (or GRAYSCALE)
csi0.framesize(csi0.framesize_list()[0])  # smallest resolution this Android camera supports
csi0.snapshot(time=2000)  # Wait for settings take effect.

# Initialize the lcd screen.
lcd = display.SPIDisplay(vflip=True, hmirror=True)

# Android cameras have no 64x64 mode: use a 64x64 patch from the frame center.
def patch():
    frame = csi0.snapshot()
    roi = (frame.width() // 2 - 32, frame.height() // 2 - 32, 64, 64)
    return frame, frame.copy(roi=roi)


clock = time.clock()  # Create a clock object to track the FPS.

# Create a second frame buffer on the heap.
extra_fb = patch()[1]

while True:
    clock.tick()  # Track elapsed milliseconds between snapshots().
    frame, img = patch()  # Take a picture, keep the center patch.

    displacement = extra_fb.find_displacement(img)
    extra_fb = img
    lcd.write(frame, hint=image.CENTER | image.SCALE_ASPECT_KEEP)

    # Offset results are noisy without filtering so we drop some accuracy.
    sub_pixel_x = int(displacement.x_translation * 5) / 5.0
    sub_pixel_y = int(displacement.y_translation * 5) / 5.0

    if (
        displacement.response > 0.1
    ):  # Below 0.1 or so (YMMV) and the results are just noise.
        print(
            "{0:+f}x {1:+f}y {2} {3} FPS".format(
                sub_pixel_x, sub_pixel_y, displacement.response, clock.fps()
            )
        )
    else:
        print(clock.fps())
