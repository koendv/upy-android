# find_hog.py
#
# - in Files, tap find_hog.py -> Run
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
# Histogram of Oriented Gradients (HoG) Example
#
# This example demonstrates HoG visualization.
#
# Note: Due to JPEG artifacts, the HoG visualization looks blurry. To see the
# image without JPEG artifacts, uncomment the lines that save the image to uSD.

import camera
import time
import display
import image

cam = camera.Camera(size=(320, 240), format=camera.GRAYSCALE)

# Initialize the lcd screen.
lcd = display.SPIDisplay()

clock = time.clock()  # Tracks FPS.

while True:
    clock.tick()
    img = cam.snapshot()
    img.find_hog()
    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)

    # Uncomment to save raw FB to file and exit the loop
    # img.save("hog.pgm")
    # break

    print(clock.fps())
