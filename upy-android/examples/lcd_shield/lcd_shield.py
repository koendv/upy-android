# lcd_shield.py
#
# - in Files, tap lcd_shield.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - camera.Camera() (CameraX) instead of csi.CSI(); no warm-up
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

import camera
import time
import display
import image

cam = camera.Camera(size=(128, 160), format=camera.RGB565)

# Initialize the lcd screen.
lcd = display.SPIDisplay()
clock = time.clock()

while True:
    clock.tick()
    lcd.write(cam.snapshot(), hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print(clock.fps(), "fps")
