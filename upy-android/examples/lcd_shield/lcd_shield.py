# lcd_shield.py
#
# - in Files, tap lcd_shield.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - csi0.framesize() picks this device's smallest supported resolution instead of a fixed resolution
#
# This work is licensed under the MIT license.
# Copyright (c) 2013-2025 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# LCD Shield Example
#
# Note: To run this example you will need a LCD Shield for your OpenMV Cam.
#
# The LCD shield allows you to view your OpenMV Cam's frame buffer on the go.

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

while True:
    clock.tick()
    lcd.write(csi0.snapshot(), hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print(clock.fps(), "fps")
