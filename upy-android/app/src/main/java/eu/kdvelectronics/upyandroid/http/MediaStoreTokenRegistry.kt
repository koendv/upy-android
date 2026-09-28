package eu.kdvelectronics.upyandroid.http

import android.content.Context
import org.json.JSONObject
import java.io.File
import java.security.SecureRandom

// Bridges android.mediastore (written from the :engine process, via
// MediaStoreShim.kt's saveImage()) to the HTTP server's GET /media
// route (read from the default/main process, via HttpServerManager.kt).
// Both processes share the same physical filesDir (Android's per-app
// data directory is keyed by package/UID, not by process name), so a
// plain file-per-token directory under it is a real, if informal, IPC
// channel. No AIDL/Binder plumbing needed for something this simple.
// One file per token (not one shared index file) is deliberate: each
// save_image() call only ever creates a brand new file, so there is no
// shared mutable state for a concurrent read (an HTTP GET /media
// request) and a concurrent write (another save_image() call, from a
// different process) to corrupt.
//
// Tokens are non-enumerable random tokens for served MediaStore items
// (not sequential/guessable IDs): 24 bytes of SecureRandom, base64url-
// encoded, one per saved item. Knowing one token reveals nothing about
// any other.
object MediaStoreTokenRegistry {
    data class Entry(val token: String, val uri: String, val displayName: String, val mimeType: String)

    private const val DIR_NAME = ".http_media_tokens"
    private val secureRandom = SecureRandom()

    private fun dir(context: Context): File =
        File(context.filesDir, DIR_NAME).apply { mkdirs() }

    // Called once per successful save_image(). See MediaStoreShim.kt.
    fun register(context: Context, uri: String, displayName: String, mimeType: String): String {
        val tokenBytes = ByteArray(24)
        secureRandom.nextBytes(tokenBytes)
        val token = android.util.Base64.encodeToString(
            tokenBytes, android.util.Base64.URL_SAFE or android.util.Base64.NO_WRAP or android.util.Base64.NO_PADDING
        )
        val json = JSONObject().apply {
            put("uri", uri)
            put("name", displayName)
            put("mime", mimeType)
        }
        File(dir(context), token).writeText(json.toString())
        return token
    }

    // Called from HttpServerManager's GET /media/{token} route.
    fun resolve(context: Context, token: String): Entry? {
        // Reject anything that isn't a plain token filename outright.
        // token is used directly as a File name below, so this is the
        // one path-traversal check that matters (e.g. a request for
        // "../../shared_prefs/upy_android_settings.xml" must never
        // reach File() at all).
        if (token.isEmpty() || token.any { it == '/' || it == '\\' || it == '.' }) {
            return null
        }
        val file = File(dir(context), token)
        if (!file.isFile) {
            return null
        }
        val json = JSONObject(file.readText())
        return Entry(token, json.getString("uri"), json.getString("name"), json.getString("mime"))
    }

    // Called from HttpServerManager's GET /media route (listing).
    fun list(context: Context): List<Entry> =
        dir(context).listFiles()?.mapNotNull { file ->
            runCatching {
                val json = JSONObject(file.readText())
                Entry(file.name, json.getString("uri"), json.getString("name"), json.getString("mime"))
            }.getOrNull()
        } ?: emptyList()
}
