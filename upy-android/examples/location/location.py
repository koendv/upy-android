# location.py
#
# - in Files, tap location.py -> Run
# - first run: allow location access in the prompt, then run again
# - prints a fix every second; stop with Interrupt
#
# android.location is a thin layer over Android's LocationManager:
#   start(interval_ms=1000), read(), last(), stop()
# A fix is (latitude, longitude, altitude_m, accuracy_m, speed_mps,
# bearing_deg, time_ms, provider); a value is None if Android has none.
# GPS may need a minute and a view of the sky for its first fix.

import time
import android

print("last known:", android.location.last())

android.location.start(interval_ms=1000)
try:
    while True:
        fix = android.location.read()
        if fix is None:
            print("waiting for a fix...")
        else:
            lat, lon, alt, acc, speed, bearing, t, provider = fix
            print("%.6f %.6f  alt %s m  accuracy %s m  (%s)" % (lat, lon, alt, acc, provider))
        time.sleep(1)
finally:
    android.location.stop()
