# mqtt_tls.py
#
# - in Files, tap mqtt_tls.py -> Run
# - needs a network connection; prints PASS or FAIL
#
# umqtt over TLS (ssl=True: Android's CA certificates, hostname checked)
# to the public test broker test.mosquitto.org, port 8886 (Let's Encrypt
# certificate, no login). Sends a message to itself, then unsubscribes.
# Also sets a last will; the broker publishes it only if this client
# drops without disconnect().

import time
import umqtt

BROKER = "test.mosquitto.org"
PORT = 8886
TOPIC = b"upy-android/tls-test"

received = []


def cb(topic, msg):
    received.append((topic, msg))


c = umqtt.MQTTClient("upy-android-tls", BROKER, PORT, ssl=True, keepalive=60)
c.set_callback(cb)
c.set_last_will(TOPIC + b"/status", b"offline", retain=True)
try:
    c.connect(timeout=10)
    c.subscribe(TOPIC)
    c.publish(TOPIC, b"hello over TLS")
    for _ in range(20):
        c.check_msg()
        if received:
            break
        time.sleep(0.5)
    c.unsubscribe(TOPIC)
    c.disconnect()
except OSError as e:
    print("FAIL:", e)
else:
    print("PASS:" if received else "FAIL: no message received", received)
