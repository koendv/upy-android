# test: required
#
# android.help() and the MicroPython modules it says are not there.
import android


def importable(name):
    try:
        __import__(name)
    except ImportError:
        return False
    return True


text = android.help()
print("help:", text.startswith("module: android"))
for name in ("socket", "ssl", "machine", "network", "requests", "_thread"):
    print("%s listed and absent:" % name, name in text and not importable(name))
print("http and umqtt exist:", importable("http") and importable("umqtt"))
