@file:JvmName("MqttShim")
// Thin JNI-facing glue for umqtt (mqtt_module.cpp/mqtt_jni_bridge.cpp).
// The only file in the app that imports com.hivemq.client.*. Every
// function here is flat/top-level, JNI-simple parameter/return types
// (String, ByteArray, Int, Boolean, Long via the opaque MqttConnection
// object reference) -- same convention as LiteRtShim.kt.
//
// MQTT 3.1.1 only (useMqttVersion3()), matching real umqtt.simple's own
// protocol version -- HiveMQ's client also supports MQTT 5, not used
// here.
//
// check_msg()/wait_msg() need no manual message queue or subscribe-time
// callback at all: HiveMQ's own Mqtt3BlockingClient.Mqtt3Publishes
// (obtained via publishes(MqttGlobalPublishFilter.ALL) right after a
// successful connect) is already exactly the "poll for one queued
// incoming message, blocking or not" primitive umqtt.simple's own
// wait_msg()/check_msg() need -- ALL (not SUBSCRIBED) matches
// umqtt.simple's own single-callback-for-every-incoming-PUBLISH model.
package eu.kdvelectronics.upyandroid.mqtt

import com.hivemq.client.mqtt.MqttClient
import com.hivemq.client.mqtt.MqttGlobalPublishFilter
import com.hivemq.client.mqtt.datatypes.MqttQos
import com.hivemq.client.mqtt.mqtt3.Mqtt3BlockingClient
import java.util.concurrent.TimeUnit

class MqttConnection(val client: Mqtt3BlockingClient) {
    var publishes: Mqtt3BlockingClient.Mqtt3Publishes? = null
}

class MqttMessage(val topic: String, val payload: ByteArray)

// MqttQos.fromCode() returns null for anything outside 0..2 -- surfaced
// here as a real, catchable exception (caught by mqtt_jni_bridge.cpp's
// ExceptionCheck() same as any other Kotlin exception) rather than a
// bare NPE, so a script passing a bad qos gets an OSError with a
// meaningful message, not "unknown error".
fun qosFromCode(code: Int): MqttQos =
    MqttQos.fromCode(code) ?: throw IllegalArgumentException("invalid qos: $code (must be 0, 1, or 2)")

fun create(clientId: String, host: String, port: Int): MqttConnection {
    val client = MqttClient.builder()
        .useMqttVersion3()
        .identifier(clientId)
        .serverHost(host)
        .serverPort(port)
        .buildBlocking()
    return MqttConnection(client)
}

// Returns session_present. username == null means a fully anonymous
// connection (real umqtt.simple supports this too); password is only
// ever applied when username is non-null, matching MQTT 3.1.1's own
// requirement that a password implies a username.
fun connect(conn: MqttConnection, username: String?, password: ByteArray?,
            cleanSession: Boolean, keepAliveSeconds: Int): Boolean {
    val builder = conn.client.connectWith()
        .cleanSession(cleanSession)
        .keepAlive(keepAliveSeconds)
    val ack = if (username != null) {
        var auth = builder.simpleAuth().username(username)
        if (password != null) {
            auth = auth.password(password)
        }
        auth.applySimpleAuth().send()
    } else {
        builder.send()
    }
    // Opened once per connection, right after connect -- closed again
    // in disconnect(). Must exist before check_msg()/wait_msg() can
    // poll anything.
    conn.publishes = conn.client.publishes(MqttGlobalPublishFilter.ALL)
    return ack.isSessionPresent
}

fun disconnect(conn: MqttConnection) {
    conn.publishes?.close()
    conn.publishes = null
    conn.client.disconnect()
}

fun publish(conn: MqttConnection, topic: String, payload: ByteArray, qos: Int, retain: Boolean) {
    conn.client.publishWith()
        .topic(topic)
        .qos(qosFromCode(qos))
        .retain(retain)
        .payload(payload)
        .send()
}

fun subscribe(conn: MqttConnection, topic: String, qos: Int) {
    conn.client.subscribeWith()
        .topicFilter(topic)
        .qos(qosFromCode(qos))
        .send()
}

// timeoutMs < 0 blocks indefinitely (wait_msg() -- called in a chunked
// loop from the native side, not with a truly indefinite value, so a
// pending interrupt can still be observed between chunks -- see
// mqtt_module.cpp's own wait_msg() comment); timeoutMs == 0 polls once,
// right now, without waiting (check_msg()). Returns null on timeout/no
// message queued, matching umqtt.simple's own check_msg() returning
// None when nothing is available.
fun poll(conn: MqttConnection, timeoutMs: Long): MqttMessage? {
    val publishes = conn.publishes ?: return null
    val publish = if (timeoutMs < 0) {
        publishes.receive()
    } else if (timeoutMs == 0L) {
        publishes.receiveNow().orElse(null)
    } else {
        publishes.receive(timeoutMs, TimeUnit.MILLISECONDS).orElse(null)
    } ?: return null
    return MqttMessage(publish.topic.toString(), publish.payloadAsBytes)
}

fun isConnected(conn: MqttConnection): Boolean = conn.client.state.isConnected
