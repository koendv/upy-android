# umqtt confidence test. Confirms the JNI/Kotlin bridge (MqttShim.kt,
# mqtt_jni_bridge.cpp) actually connects to a real external MQTT broker
# over a real network, not just that the code compiles.
#
# Uses a public test broker (test.mosquitto.org, plain TCP, no auth)
# and two retained messages so this script's own network round trip
# can be independently cross-checked from a completely separate MQTT
# client driven from the host machine: a retained "ping" message is
# published from the host before this script runs, and this script
# publishes a retained "pong" reply the host checks for afterward.
# Retained delivery means the exact order or timing between the two
# clients doesn't matter, only that both sides genuinely reach the
# same real broker.
import time
import umqtt

BROKER = "test.mosquitto.org"
TOPIC_PING = "upy-android/selftest/ping"
TOPIC_PONG = "upy-android/selftest/pong"
EXPECTED_PING = b"upy-android-ping"
PONG_PAYLOAD = b"upy-android-pong"


def main():
    print("umqtt selftest")
    c = umqtt.MQTTClient("upy-android-selftest", BROKER, 1883)
    try:
        c.connect()
        c.subscribe(TOPIC_PING)

        received = {}

        def cb(topic, msg):
            received["topic"] = topic
            received["msg"] = msg

        c.set_callback(cb)

        got = False
        for _ in range(20):
            c.check_msg()
            if "msg" in received:
                got = True
                break
            time.sleep(0.5)

        if not got:
            print("FAIL: did not receive retained ping within timeout")
            c.disconnect()
            return

        if received["msg"] != EXPECTED_PING:
            print("FAIL: unexpected ping payload", received["msg"])
            c.disconnect()
            return

        c.publish(TOPIC_PONG, PONG_PAYLOAD, retain=True)
        c.disconnect()
    except OSError as e:
        print("FAIL:", e)
        return

    print("PASS")


main()
