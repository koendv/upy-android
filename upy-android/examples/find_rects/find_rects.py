# find_rects.py
#
# - in Files, tap find_rects.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - camera.Camera() (CameraX) instead of csi.CSI(); no warm-up
# - frames are shown on the Camera tab via display.SPIDisplay
#
# This work is licensed under the MIT license.
# Copyright (c) 2013-2023 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# Find Rects Example
#
# This example shows off how to find rectangles in the image using the quad threshold
# detection code from our April Tags code. The quad threshold detection algorithm
# detects rectangles in an extremely robust way and is much better than Hough
# Transform based methods. For example, it can still detect rectangles even when lens
# distortion causes those rectangles to look bent. Rounded rectangles are no problem!
# (But, given this the code will also detect small radius circles too)...

import camera
import time
import display
import image

cam = camera.Camera(size=(160, 120), format=camera.RGB565)  # grayscale is faster (160x120 max on OpenMV-M7)

# Initialize the lcd screen.
lcd = display.SPIDisplay()

clock = time.clock()

while True:
    clock.tick()
    img = cam.snapshot()

    # `threshold` below should be set to a high enough value to filter out noise
    # rectangles detected in the image which have low edge magnitudes. Rectangles
    # have larger edge magnitudes the larger and more contrasty they are...

    for r in img.find_rects(threshold=10000):
        img.draw_rectangle(r, color=(255, 0, 0))
        for p in r.corners:
            img.draw_circle((p[0], p[1], 5), color=(0, 255, 0))
        print(r)

    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)

    print("FPS %f" % clock.fps())
