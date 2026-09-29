# find_hog.py
#
# - in Files, tap find_hog.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - csi0.framesize() picks this device's smallest supported resolution instead of a fixed resolution
# - csi0.contrast()/gainceiling() removed: not implemented on Android
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

import csi
import time
import display
import image

csi0 = csi.CSI()
csi0.reset()
csi0.framesize(csi0.framesize_list()[0])  # smallest resolution this Android camera supports
csi0.pixformat(csi.GRAYSCALE)
csi0.snapshot(time=2000)

# Initialize the lcd screen.
lcd = display.SPIDisplay(vflip=True, hmirror=True)

clock = time.clock()  # Tracks FPS.

while True:
    clock.tick()
    img = csi0.snapshot()
    img.find_hog()
    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)

    # Uncomment to save raw FB to file and exit the loop
    # img.save("hog.pgm")
    # break

    print(clock.fps())
