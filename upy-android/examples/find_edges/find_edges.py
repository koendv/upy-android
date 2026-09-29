# find_edges.py
#
# - in Files, tap find_edges.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - csi0.framesize() picks this device's smallest supported resolution instead of a fixed resolution
# - frames are shown on the Camera tab via display.SPIDisplay
# - csi0.gainceiling() removed: not implemented on Android
#
# This work is licensed under the MIT license.
# Copyright (c) 2013-2023 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# Edge detection with Canny:
#
# This example demonstrates the Canny edge detector.
import csi
import image
import time
import display

csi0 = csi.CSI()
csi0.reset()  # Initialize the camera sensor.
csi0.pixformat(csi.GRAYSCALE)  # or csi.RGB565
csi0.framesize(csi0.framesize_list()[0])  # smallest resolution this Android camera supports
csi0.snapshot(time=2000)  # Let new settings take affect.

# Initialize the lcd screen.
lcd = display.SPIDisplay(vflip=True, hmirror=True)

clock = time.clock()  # Tracks FPS.
while True:
    clock.tick()  # Track elapsed milliseconds between snapshots().
    img = csi0.snapshot()  # Take a picture and return the image.
    # Use Canny edge detector
    img.find_edges(image.EDGE_CANNY, threshold=(50, 80))
    # Faster simpler edge detection
    # img.find_edges(image.EDGE_SIMPLE, threshold=(100, 255))

    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print(clock.fps())  # Note: Your OpenMV Cam runs about half as fast while
