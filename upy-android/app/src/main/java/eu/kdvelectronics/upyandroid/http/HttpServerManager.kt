package eu.kdvelectronics.upyandroid.http

import android.content.Context
import android.net.Uri
import android.util.Log
import eu.kdvelectronics.upyandroid.managers.SettingsManager
import io.ktor.http.ContentType
import io.ktor.http.HttpStatusCode
import io.ktor.http.auth.DigestAlgorithm
import io.ktor.server.application.call
import io.ktor.server.application.install
import io.ktor.server.auth.Authentication
import io.ktor.server.auth.UserIdPrincipal
import io.ktor.server.auth.authenticate
import io.ktor.server.auth.digest
import io.ktor.server.cio.CIO
import io.ktor.server.engine.EmbeddedServer
import io.ktor.server.engine.embeddedServer
import io.ktor.server.plugins.statuspages.StatusPages
import io.ktor.server.response.respond
import io.ktor.server.response.respondSource
import io.ktor.server.response.respondText
import io.ktor.server.routing.get
import io.ktor.server.routing.routing
import io.ktor.util.StatelessHmacNonceManager
import kotlinx.io.asSource
import java.io.File
import java.io.FileNotFoundException
import java.security.MessageDigest
import java.security.SecureRandom

// see session-state: HttpServerManager.kt#HttpServerManager
object HttpServerManager {
    private const val TAG = "HttpServerManager"
    const val HTTP_PORT = 8080

    @Volatile
    private var server: EmbeddedServer<*, *>? = null

    // see session-state: HttpServerManager.kt#HttpServerManager
    @Synchronized
    fun applySettings(context: Context, settingsManager: SettingsManager) {
        val shouldRun = settingsManager.httpServerEnabled
        val running = server != null
        if (shouldRun && !running) {
            start(context, settingsManager)
        } else if (!shouldRun && running) {
            stop()
        }
    }

    private fun start(context: Context, settingsManager: SettingsManager) {
        Log.i(TAG, "starting HTTP server on port $HTTP_PORT")
        server = embeddedServer(CIO, port = HTTP_PORT) {
            install(StatusPages) {
                exception<FileNotFoundException> { call, _ ->
                    call.respond(HttpStatusCode.NotFound)
                }
                exception<Throwable> { call, cause ->
                    Log.w(TAG, "request failed", cause)
                    call.respond(HttpStatusCode.InternalServerError)
                }
            }
            install(Authentication) {
                // Digest, not Basic: the password does not cross the network.
                // Any user name; nonces expire, signed with a per-start key.
                digest("http-password") {
                    realm = "upy-android"
                    nonceManager = StatelessHmacNonceManager(ByteArray(32).also { SecureRandom().nextBytes(it) })
                    // Android has no SHA-512/256, Ktor's first default. MD5
                    // first: Python's urllib reads only the first challenge.
                    algorithms = listOf(DigestAlgorithm.MD5, DigestAlgorithm.SHA_256)
                    digestProvider { userName, realm, algorithm ->
                        // Empty http_password means "not configured": deny every request.
                        val configured = settingsManager.httpPassword
                        if (configured.isEmpty()) {
                            null
                        } else {
                            MessageDigest.getInstance(algorithm.hashName).digest("$userName:$realm:$configured".toByteArray(Charsets.UTF_8))
                        }
                    }
                    validate { credentials -> UserIdPrincipal(credentials.userName) }
                }
            }
            routing {
                authenticate("http-password") {
                    get("/media") {
                        val entries = MediaStoreTokenRegistry.list(context)
                        val body = buildString {
                            entries.forEach { append(it.token).append('\t').append(it.displayName).append('\n') }
                        }
                        call.respondText(body, ContentType.Text.Plain)
                    }
                    get("/media/{token}") {
                        val token = call.parameters["token"] ?: return@get call.respond(HttpStatusCode.NotFound)
                        val entry = MediaStoreTokenRegistry.resolve(context, token)
                            ?: return@get call.respond(HttpStatusCode.NotFound)
                        val stream = context.contentResolver.openInputStream(Uri.parse(entry.uri))
                            ?: return@get call.respond(HttpStatusCode.NotFound)
                        // respondSource takes ownership of the RawSource
                        // (streamed straight from the real content://
                        // URI, no intermediate copy) and closes it once
                        // fully consumed. Not wrapped in .use{} here,
                        // which would close it before Ktor finishes reading.
                        call.respondSource(stream.asSource(), ContentType.parse(entry.mimeType))
                    }
                    get("/files/{path...}") {
                        if (!settingsManager.httpPrivateFilesEnabled) {
                            return@get call.respond(HttpStatusCode.Forbidden)
                        }
                        val relativePath = call.parameters.getAll("path")?.joinToString("/") ?: ""
                        // File(base, child) converts an absolute child
                        // pathname into a relative one before resolving
                        // it against base (see FileProviderShim.kt's own
                        // comment on this same javadoc guarantee). A
                        // request path can't escape filesDir via a
                        // leading "/", only via "..", checked next.
                        if (relativePath.split('/').any { it == ".." }) {
                            return@get call.respond(HttpStatusCode.BadRequest)
                        }
                        val file = File(context.filesDir, relativePath)
                        if (!file.isFile) {
                            return@get call.respond(HttpStatusCode.NotFound)
                        }
                        // No MIME sniffing/extension mapping for v1.
                        // The consumer already knows what it asked for.
                        call.respondSource(file.inputStream().asSource(), ContentType.Application.OctetStream)
                    }
                }
            }
        }.start(wait = false)
    }

    private fun stop() {
        Log.i(TAG, "stopping HTTP server")
        server?.stop(gracePeriodMillis = 1000, timeoutMillis = 2000)
        server = null
    }
}
