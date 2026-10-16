# test: required
#
# imu is a top-level module, like OpenMV's imu; android.imu is gone. The
# phone lies still: gravity is about 1000 mg. A phone without a gyroscope
# answers (0, 0, 0).
import android
import imu

print("android.imu gone:", not hasattr(android, "imu"))
x, y, z = imu.acceleration_mg()
g = (x * x + y * y + z * z) ** 0.5
print("acceleration about 1 g:", 700 < g < 1300)
print("roll in range:", 0 <= imu.roll() < 360)
print("pitch in range:", -180 <= imu.pitch() <= 180)
print("angular rate has 3 values:", len(imu.angular_rate_mdps()) == 3)
print("help:", imu.help().startswith("module: imu"))
