# upy-android: verified working on real Android hardware -- run it via
# Files (tap lcd_shield.py -> Run), then switch to the Camera tab WHILE
# it's still running (the on-screen terminal's own "Run" output always
# shows first; Camera's SurfaceView only receives frames while its own
# tab is actually composed and visible, per CameraScreen.kt's own
# design). Interrupt via the Camera screen's own Stop button, same as
# hitting Ctrl+C on real OpenMV hardware. Only one line changed from the
# real upstream script below: csi0.framesize() picks this device's own
# smallest supported resolution instead of a fixed literal, since real
# phone cameras (unlike every real OpenMV board) don't all support the
# same fixed small sizes -- see that line's own comment.
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
# Smallest resolution this Android camera actually supports, not a
# fixed literal -- real OpenMV boards all support the same small fixed
# sizes, but real phone cameras vary, so csi.QQVGA/a hardcoded tuple can
# be unsupported (confirmed on a real device: 176x144 is the true
# smallest, not 128x160/QQVGA).
csi0.framesize(min(csi0.framesize_list(), key=lambda s: s[0] * s[1]))

# Initialize the lcd screen.
# Note: A DAC or a PWM backlight controller can be used to control the
# backlight intensity if supported:
#  lcd = display.SPIDisplay(backlight=display.DACBacklight(channel=2))
#  lcd.backlight(25) # 25% intensity
# Otherwise the default GPIO (on/off) controller is used.
#  OpenMV Cam M4/M7/H7/H7 Plus -> DAC and GPIO Support
#  OpenMV Cam RT1062 -> GPIO Support
#  OpenMV Cam N6 -> PWM and GPIO Support
lcd = display.SPIDisplay(vflip=True, hmirror=True)
clock = time.clock()

while True:
    clock.tick()
    lcd.write(csi0.snapshot(), hint=image.CENTER | image.SCALE_ASPECT_KEEP)
    print(clock.fps())
