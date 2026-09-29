# find_apriltags.py
#
# - in Files, tap find_apriltags.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - csi0.framesize() picks this device's smallest supported resolution instead of a fixed resolution
# - frames are shown on the Camera tab via display.SPIDisplay
# - csi0.auto_gain()/auto_whitebal() removed: not implemented on Android
#
# This work is licensed under the MIT license.
# Copyright (c) 2013-2023 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# AprilTags Example
#
# This example shows the power of the OpenMV Cam to detect April Tags
# on the OpenMV Cam M7. The M4 versions cannot detect April Tags.

import csi
import time
import math
import display
import image

csi0 = csi.CSI()
csi0.reset()
csi0.pixformat(csi.RGB565)
csi0.framesize(csi0.framesize_list()[0])  # smallest resolution this Android camera supports
csi0.snapshot(time=2000)

# Initialize the lcd screen.
lcd = display.SPIDisplay(vflip=True, hmirror=True)

clock = time.clock()

# Note! Unlike find_qrcodes the find_apriltags method does not need lens correction on the image to work.

# Please use the TAG36H11 tag family for this script - it's the recommended tag family to use.


while True:
    clock.tick()
    img = csi0.snapshot()
    for tag in img.find_apriltags():
        img.draw_detection(tag, color1=(255, 0, 0), color2=(0, 255, 0))
        print_args = (tag.name, tag.id, (180 * tag.rotation) / math.pi)
        print("Tag Family %s, Tag ID %d, rotation %f (degrees)" % print_args)

    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print(clock.fps())
