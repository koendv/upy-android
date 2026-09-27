# upy-android: verified working on real Android hardware -- run it via
# Files (tap lcd_shield.py -> Run), then switch to the Camera tab WHILE
# it's still running (the on-screen terminal's own "Run" output always
# shows first; Camera's SurfaceView only receives frames while its own
# tab is actually composed and visible, per CameraScreen.kt's own
# design). Interrupt via the Camera screen's own Stop button, same as
# hitting Ctrl+C on real OpenMV hardware. Two lines changed from the
# real upstream script below: csi0.framesize() picks this device's own
# smallest supported resolution instead of a fixed literal, since real
# phone cameras (unlike every real OpenMV board) don't all support the
# same fixed small sizes -- see that line's own comment; and the fps
# print includes a " fps" unit label.
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
# Smallest resolution this Android camera actually supports
csi0.framesize(min(csi0.framesize_list(), key=lambda s: s[0] * s[1]))

# Initialize the lcd screen.
lcd = display.SPIDisplay(vflip=True, hmirror=True)
clock = time.clock()

while True:
    clock.tick()
    lcd.write(csi0.snapshot(), hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print(clock.fps(), "fps")
