# find_datamatrices.py
#
# - in Files, tap find_datamatrices.py -> Run
# - switch to the Camera tab while script running
# - interrupt via the Camera screen's Stop button
#
# Change with upstream:
# - csi0.framesize() picks this device's smallest supported resolution instead of a fixed resolution
# - csi0.auto_gain()/auto_whitebal() removed: not implemented on Android
# - lens_corr() removed: not enabled on Android, phone cameras already correct lens distortion
# - frames are shown on the Camera tab via display.SPIDisplay
#
# This work is licensed under the MIT license.
# Copyright (c) 2013-2023 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# Find Data Matrices Example
#
# This example shows off how easy it is to detect data matrices using the
# OpenMV Cam M7. Data matrices detection does not work on the M4 Camera.

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

while True:
    clock.tick()
    img = csi0.snapshot()

    matrices = img.find_datamatrices()
    for matrix in matrices:
        img.draw_rectangle(matrix.rect, color=(255, 0, 0))
        print_args = (
            matrix.rows,
            matrix.columns,
            matrix.payload,
            (180 * matrix.rotation) / math.pi,
            clock.fps(),
        )
        print(
            'Matrix [%d:%d], Payload "%s", rotation %f (degrees), FPS %f' % print_args
        )
    lcd.write(img, hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    if not matrices:
        print("FPS %f" % clock.fps())
