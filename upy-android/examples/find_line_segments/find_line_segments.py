# find_line_segments.py
#
# - in Files, tap find_line_segments.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - csi0.framesize() picks this device's smallest supported resolution instead of a fixed resolution
#
# This work is licensed under the MIT license.
# Copyright (c) 2013-2023 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# Find Line Segments Example
#
# This example shows off how to find line segments in the image. For each line object
# found in the image a line object is returned which includes the line's rotation.

# find_line_segments() finds finite length lines (but is slow).

import csi
import time
import display
import image

csi0 = csi.CSI()
csi0.reset()
csi0.pixformat(csi.RGB565)
csi0.framesize(csi0.framesize_list()[0])  # smallest resolution this Android camera supports

# Initialize the lcd screen.
lcd = display.SPIDisplay(vflip=True, hmirror=True)
clock = time.clock()

# All lines also have `x1`, `y1`, `x2`, and `y2` attributes to get their end-points.
# Line objects can be passed directly to `draw_line()`.

while True:
    clock.tick()
    img = csi0.snapshot()

    # `merge_distance` controls the merging of nearby lines. At 0 (the default), no
    # merging is done. At 1, any line 1 pixel away from another is merged... and so
    # on as you increase this value. You may wish to merge lines as line segment
    # detection produces a lot of line segment results.

    # `max_theta_diff` controls the maximum amount of rotation difference between
    # any two lines about to be merged. The default setting allows for 15 degrees.

    for l in img.find_line_segments(merge_distance=0, max_theta_diff=5):
        img.draw_line(l, color=(255, 0, 0))

    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print(clock.fps(), "fps")
