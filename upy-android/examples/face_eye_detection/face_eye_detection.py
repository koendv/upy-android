# face_eye_detection.py
#
# - in Files, tap face_eye_detection.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
# - hold the phone so the camera image is upright: Haar cascades only detect upright faces
#
# Change with upstream:
# - camera.Camera() (CameraX) instead of csi.CSI(); no warm-up
# - frames are shown on the Camera tab via display.SPIDisplay
#
# This work is licensed under the MIT license.
# Copyright (c) 2013-2023 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# Face Eye Detection Example
#
# This script uses the built-in frontalface detector to find a face and then
# the eyes within the face. If you want to determine the eye gaze please see the
# iris_detection script for an example on how to do that.

import camera
import time
import display
import image

cam = camera.Camera(size=(240, 160), format=camera.GRAYSCALE)

# Load Haar Cascade
# By default this will use all stages, lower satges is faster but less accurate.
face_cascade = image.HaarCascade("/rom/haarcascade_frontalface.cascade", stages=25)
eyes_cascade = image.HaarCascade("/rom/haarcascade_eye.cascade", stages=24)
print(face_cascade, eyes_cascade)

# Initialize the lcd screen.
lcd = display.SPIDisplay()

# FPS clock
clock = time.clock()

while True:
    clock.tick()

    # Capture snapshot
    img = cam.snapshot()

    # Find a face !
    # Note: Lower scale factor scales-down the image more and detects smaller objects.
    # Higher threshold results in a higher detection rate, with more false positives.
    objects = img.find_features(face_cascade, threshold=0.5, scale=1.5)

    # Draw faces
    for face in objects:
        img.draw_rectangle(face)
        # Now find eyes within each face.
        # Note: Use a higher threshold here (more detections) and lower scale (to find small objects)
        eyes = img.find_features(
            eyes_cascade, threshold=0.5, scale=1.2, roi=face
        )
        for e in eyes:
            img.draw_rectangle(e)

    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)

    # Print FPS.
    # Note: Actual FPS is higher, streaming the FB makes it slower.
    print(clock.fps())
