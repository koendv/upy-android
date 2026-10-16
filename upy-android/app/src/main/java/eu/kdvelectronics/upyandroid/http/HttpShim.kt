@file:JvmName("HttpShim")
// JNI-facing glue for the http module (http_module.cpp, http_jni_bridge.cpp).
// Flat, top-level functions with JNI-simple types, like MqttShim.kt. Every
// call blocks the interpreter thread that made it; cancel() ends the running
// call from another thread (Interrupt).
package eu.kdvelectronics.upyandroid.http

import eu.kdvelectronics.upyandroid.VfsPath
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit
import okhttp3.Call
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.ResponseBody

// Shown to the script as the OSError message, so no class name in it.
class HttpError(message: String) : IOException(message) {
    override fun toString() = message ?: ""
}

// get() and post() answer in memory; native turns this into a MemoryError.
class ResponseTooLarge(message: String) : IOException(message) {
    override fun toString() = message ?: ""
}

// The bytes go to the log, from this (interpreter) thread. See http_jni_bridge.cpp.
external fun nativePrint(text: String)

private val baseClient = OkHttpClient()

// The call that is running, and whether cancel() was called since begin().
// cancel() sets the flag and then reads active; a call sets active and then
// reads the flag, so an Interrupt that arrives at any moment cancels the call.
@Volatile
private var active: Call? = null

@Volatile
private var cancelled = false

// Redirects (also between http and https) are followed and the system trust
// store is used: okhttp's defaults. The read timeout is per read, not for the
// whole transfer. Zero or less: no timeout.
private fun call(request: Request, timeoutSeconds: Double): Call {
    val ms = (timeoutSeconds * 1000).toLong().coerceIn(0L, Int.MAX_VALUE.toLong())
    val client = baseClient.newBuilder()
        .connectTimeout(ms, TimeUnit.MILLISECONDS)
        .readTimeout(ms, TimeUnit.MILLISECONDS)
        .writeTimeout(ms, TimeUnit.MILLISECONDS)
        .build()
    return client.newCall(request)
}

private fun <T> execute(request: Request, timeoutSeconds: Double, use: (ResponseBody) -> T): T {
    val call = call(request, timeoutSeconds)
    active = call
    if (cancelled) call.cancel()
    try {
        call.execute().use { response ->
            if (response.code >= 400) throw HttpError("HTTP ${response.code}")
            return use(response.body!!)
        }
    } finally {
        active = null
    }
}

private fun readLimited(body: ResponseBody, maxBytes: Long): ByteArray {
    val tooLarge = "response larger than the free memory, use http.download"
    if (body.contentLength() > maxBytes) throw ResponseTooLarge(tooLarge)
    val out = ByteArrayOutputStream()
    val buffer = ByteArray(64 * 1024)
    body.byteStream().use { input ->
        while (true) {
            val n = input.read(buffer)
            if (n < 0) break
            out.write(buffer, 0, n)
            if (out.size() > maxBytes) throw ResponseTooLarge(tooLarge)
        }
    }
    return out.toByteArray()
}

fun get(url: String, timeoutSeconds: Double, maxBytes: Long): ByteArray =
    execute(Request.Builder().url(url).build(), timeoutSeconds) { readLimited(it, maxBytes) }

fun post(url: String, data: ByteArray, contentType: String, timeoutSeconds: Double, maxBytes: Long): ByteArray {
    val request = Request.Builder().url(url).post(data.toRequestBody(contentType.toMediaType())).build()
    return execute(request, timeoutSeconds) { readLimited(it, maxBytes) }
}

// Streams to a temporary file next to path and renames it when complete, so a
// failed download leaves no partial file.
fun download(url: String, path: String, progress: Boolean, timeoutSeconds: Double) {
    val target = VfsPath.resolve(path)
    val temp = File(target.parentFile, target.name + ".part")
    try {
        execute(Request.Builder().url(url).build(), timeoutSeconds) { body ->
            val total = body.contentLength()
            var done = 0L
            var nextReport = 0L
            var lastPrinted = -1L
            val step = if (total > 0) total / 10 + 1 else 1024L * 1024
            fun report() {
                nativePrint(if (total > 0) "${done * 100 / total}% ($done of $total bytes)\n" else "$done bytes\n")
                lastPrinted = done
            }
            body.byteStream().use { input ->
                temp.outputStream().use { output ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        output.write(buffer, 0, n)
                        done += n
                        if (progress && done >= nextReport) {
                            report()
                            nextReport = done + step
                        }
                    }
                }
            }
            if (progress && lastPrinted != done) report()
        }
        if (!temp.renameTo(target)) throw IOException("cannot write $path")
    } catch (e: IOException) {
        temp.delete()
        throw e
    }
}

// The interpreter thread, before it checks for a pending Interrupt and starts a call.
fun begin() {
    cancelled = false
}

// Any thread. Ends the running call, or the next one before it starts to
// wait; the call fails with an IOException.
fun cancel() {
    cancelled = true
    active?.cancel()
}
