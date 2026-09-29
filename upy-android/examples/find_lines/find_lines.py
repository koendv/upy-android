# find_lines.py
#
# - in Files, tap find_lines.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - csi0.framesize() picks this device's smallest supported resolution instead of a fixed resolution
# - frames are shown on the Camera tab via display.SPIDisplay
# - lens_corr() removed: not enabled on Android, phone cameras already correct lens distortion
#
# This work is licensed under the MIT license.
# Copyright (c) 2013-2023 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# Find Lines Example
#
# This example shows off how to find lines in the image. For each line object
# found in the image a line object is returned which includes the line's rotation.

# Note: Line detection is done by using the Hough Transform:
# http://en.wikipedia.org/wiki/Hough_transform
# Please read about it above for more information on what `theta` and `rho` are.

# find_lines() finds infinite length lines. Use find_line_segments() to find non-infinite lines.

import csi
import time
import display
import image


csi0 = csi.CSI()
csi0.reset()
csi0.pixformat(csi.RGB565)  # grayscale is faster
csi0.framesize(csi0.framesize_list()[0])  # smallest resolution this Android camera supports
csi0.snapshot(time=2000)

# Initialize the lcd screen.
lcd = display.SPIDisplay(vflip=True, hmirror=True)

clock = time.clock()

# All line objects have a `theta` attribute to get their rotation angle in degrees.
# You can filter lines based on their rotation angle.

min_degree = 0
max_degree = 179

# All lines also have `x1`, `y1`, `x2`, and `y2` attributes to get their end-points.
# Line objects can be passed directly to `draw_line()`.

while True:
    clock.tick()
    img = csi0.snapshot()

    # `threshold` controls how many lines in the image are found. Only lines with
    # edge difference magnitude sums greater than `threshold` are detected...

    # More about `threshold` - each pixel in the image contributes a magnitude value
    # to a line. The sum of all contributions is the magintude for that line. Then
    # when lines are merged their magnitudes are added together. Note that `threshold`
    # filters out lines with low magnitudes before merging. To see the magnitude of
    # un-merged lines set `theta_margin` and `rho_margin` to 0...

    # `theta_margin` and `rho_margin` control merging similar lines. If two lines
    # theta and rho value differences are less than the margins then they are merged.

    for l in img.find_lines(threshold=1000, theta_margin=25, rho_margin=25):
        if (min_degree <= l.theta) and (l.theta <= max_degree):
            img.draw_line(l, color=(255, 0, 0))
            # print(l)


    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print("FPS %f" % clock.fps())

# About negative rho values:
#
# A [theta+0:-rho] tuple is the same as [theta+180:+rho].
