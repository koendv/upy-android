# test: required
#
# time.sleep, sleep_ms: the pause is at least as long as asked and not
# much longer. sleep(0) returns at once.
import time


def slept(call):
    t0 = time.ticks_ms()
    call()
    return time.ticks_diff(time.ticks_ms(), t0)


for ms in (1, 10, 100, 1000):
    d = slept(lambda: time.sleep_ms(ms))
    print("sleep_ms(%d):" % ms, ms <= d <= ms + 50)

d = slept(lambda: time.sleep(0.25))
print("sleep(0.25):", 250 <= d <= 300)
d = slept(lambda: time.sleep(1))
print("sleep(1):", 1000 <= d <= 1050)
print("sleep(0):", slept(lambda: time.sleep(0)) < 5)
print("sleep_ms(0):", slept(lambda: time.sleep_ms(0)) < 5)
