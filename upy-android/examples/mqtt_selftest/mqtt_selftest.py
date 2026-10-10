# test: manual needs internet and test.mosquitto.org
#
# umqtt round trip through a real broker, plain (port 1883) and TLS
# (port 8886, Android's CA certificates). The phone subscribes to a new
# random topic, publishes one message to it and waits for that message.
import random
import time
import umqtt

BROKER = "test.mosquitto.org"


def round_trip(name, port, ssl):
    tag = "%08x" % random.getrandbits(32)
    topic = "upy-android/test/" + tag
    payload = ("upy-android " + tag).encode()
    received = []
    c = umqtt.MQTTClient("upy-android-test-" + tag, BROKER, port, ssl=ssl)
    c.set_callback(lambda t, m: received.append((t, m)))
    try:
        c.connect(timeout=10)
        c.subscribe(topic)
        c.publish(topic, payload)
        for _ in range(20):
            c.check_msg()
            if received:
                break
            time.sleep(0.5)
        c.disconnect()
    except OSError as e:
        print(name + ": FAIL", e)
        return
    if not received:
        print(name + ": FAIL no message within 10 s")
    elif received[0] != (topic.encode(), payload):
        print(name + ": FAIL wrong message", received[0])
    else:
        print(name + ": PASS")


round_trip("plain", 1883, False)
round_trip("tls", 8886, True)
