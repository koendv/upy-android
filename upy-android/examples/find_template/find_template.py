# find_template.py
#
# - in Files, tap find_template.py -> Run
# - switch to the Camera tab while script running
# - first run: aim the box at an object; after 5 seconds the box contents
#   are saved as /template.pgm
# - later runs: the saved template is searched for in each frame
# - delete /template.pgm in Files to capture a new template
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - csi0.framesize() picks this device's smallest supported resolution instead of a fixed resolution
# - csi0.contrast()/gainceiling() removed: not implemented on Android
# - /template.pgm is captured from the camera on first run instead of prepared beforehand
# - frames are shown on the Camera tab via display.SPIDisplay
#
# This work is licensed under the MIT license.
# Copyright (c) 2013-2023 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# Template Matching Example - Normalized Cross Correlation (NCC)
#
# This example shows off how to use the NCC feature of your OpenMV Cam to match
# image patches to parts of an image... expect for extremely controlled environments
# NCC is not all to useful.
#
# Template matching is neither scale nor rotation invariant: the object must
# appear at the same size and orientation as when the template was captured.

import os
import time
import csi
import image
import display
from image import SEARCH_EX

TEMPLATE = "/template.pgm"
TEMPLATE_W = 40
TEMPLATE_H = 30

csi0 = csi.CSI()
csi0.reset()
csi0.framesize(csi0.framesize_list()[0])  # smallest resolution this Android camera supports
csi0.pixformat(csi.GRAYSCALE)

# Initialize the lcd screen.
lcd = display.SPIDisplay(vflip=True, hmirror=True)


def exists(path):
    try:
        os.stat(path)
        return True
    except OSError:
        return False


if not exists(TEMPLATE):
    # Show a box in the frame center for 5 seconds, then save its contents.
    start = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), start) < 5000:
        img = csi0.snapshot()
        box = ((img.width() - TEMPLATE_W) // 2, (img.height() - TEMPLATE_H) // 2, TEMPLATE_W, TEMPLATE_H)
        lcd.write(img.copy().draw_rectangle(box), hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    img.save(TEMPLATE, roi=box)
    print("saved", TEMPLATE)

template = image.Image(TEMPLATE)

clock = time.clock()

while True:
    clock.tick()
    img = csi0.snapshot()

    # find_template(template, threshold, [roi, step, search])
    # ROI: The region of interest tuple (x, y, w, h).
    # Step: The loop step used (y+=step, x+=step) use a bigger step to make it faster.
    # Search is either image.SEARCH_EX for exhaustive search or image.SEARCH_DS for diamond search
    #
    # Note1: ROI has to be smaller than the image and bigger than the template.
    # Note2: In diamond search, step and ROI are both ignored.
    r = img.find_template(template, 0.70, step=4, search=SEARCH_EX)
    if r:
        img.draw_rectangle(r)

    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print(clock.fps())
