# find_edges.py
#
# - in Files, tap find_edges.py -> Run
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
# Edge detection with Canny:
#
# This example demonstrates the Canny edge detector.
import camera
import image
import time
import display

cam = camera.Camera(size=(160, 120), format=camera.GRAYSCALE)  # or camera.RGB565

# Initialize the lcd screen.
lcd = display.SPIDisplay()

clock = time.clock()  # Tracks FPS.
while True:
    clock.tick()  # Track elapsed milliseconds between snapshots().
    img = cam.snapshot()  # Take a picture and return the image.
    # Use Canny edge detector
    img.find_edges(image.EDGE_CANNY, threshold=(50, 80))
    # Faster simpler edge detection
    # img.find_edges(image.EDGE_SIMPLE, threshold=(100, 255))

    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print(clock.fps())  # Note: Your OpenMV Cam runs about half as fast while
