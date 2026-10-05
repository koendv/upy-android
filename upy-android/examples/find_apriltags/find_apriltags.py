# find_apriltags.py
#
# - in Files, tap find_apriltags.py -> Run
# - switch to the Camera tab while script running
# - point the camera at a page or screen showing TAG36H11 tags
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
# AprilTags Example
#
# This example shows the power of the OpenMV Cam to detect April Tags
# on the OpenMV Cam M7. The M4 versions cannot detect April Tags.

import camera
import time
import math
import display
import image

cam = camera.Camera(size=(160, 120), format=camera.RGB565)

# Initialize the lcd screen.
lcd = display.SPIDisplay()

clock = time.clock()

# Note! Unlike find_qrcodes the find_apriltags method does not need lens correction on the image to work.

# Please use the TAG36H11 tag family for this script - it's the recommended tag family to use.


while True:
    clock.tick()
    img = cam.snapshot()
    for tag in img.find_apriltags():
        img.draw_detection(tag, color1=(255, 0, 0), color2=(0, 255, 0))
        print_args = (tag.name, tag.id, (180 * tag.rotation) / math.pi)
        print("Tag Family %s, Tag ID %d, rotation %f (degrees)" % print_args)

    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print(clock.fps())
