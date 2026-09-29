# location.py
#
# - in Files, tap location.py -> Run
# - first run: allow location access in the prompt, then run again
# - prints a fix each time the phone has moved 0.5 m; stop with Interrupt
#
# android.location is a thin layer over Android's LocationManager:
#   start(interval_ms=1000, min_distance_m=0), read(timeout_ms=0), last(), stop()
# A fix is (latitude, longitude, altitude_m, accuracy_m, speed_mps,
# bearing_deg, time_ms, provider); a value is None if Android has none.
# read() returns only a fix it has not returned before: at once, or after
# waiting up to timeout_ms (-1: until one arrives); None if there is none.
# With min_distance_m, Android sends no new fix until the phone has moved
# that far.
# GPS may need a minute and a view of the sky for its first fix. Its
# accuracy is typically several meters, so a 0.5 m step includes jitter.

import android

print("last known:", android.location.last())

android.location.start(interval_ms=1000, min_distance_m=0.5)
try:
    while True:
        lat, lon, alt, acc, speed, bearing, t, provider = android.location.read(timeout_ms=-1)
        print("%.6f %.6f  alt %s m  accuracy %s m  (%s)" % (lat, lon, alt, acc, provider))
finally:
    android.location.stop()
