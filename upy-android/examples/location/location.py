# location.py
#
# - in Files, tap location.py -> Run
# - first run: allow location access in the prompt, then run again
# - prints a fix each time the phone has moved 0.5 m; stop with Interrupt
#
# android.location is a thin layer over Android's LocationManager:
#   start(interval_ms=1000, min_distance_m=0), read(), last(), stop()
# A fix is (latitude, longitude, altitude_m, accuracy_m, speed_mps,
# bearing_deg, time_ms, provider); a value is None if Android has none.
# With min_distance_m, Android sends no new fix until the phone has moved
# that far; read() keeps returning the previous one, so a new time_ms
# means a new fix.
# GPS may need a minute and a view of the sky for its first fix. Its
# accuracy is typically several meters, so a 0.5 m step includes jitter.

import time
import android

print("last known:", android.location.last())

android.location.start(interval_ms=1000, min_distance_m=0.5)
shown = None
try:
    while True:
        fix = android.location.read()
        if fix is not None and fix[6] != shown:
            lat, lon, alt, acc, speed, bearing, t, provider = fix
            print("%.6f %.6f  alt %s m  accuracy %s m  (%s)" % (lat, lon, alt, acc, provider))
            shown = t
        time.sleep(0.2)
finally:
    android.location.stop()
