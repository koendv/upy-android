package eu.kdvelectronics.upyandroid.http

import android.content.Context
import android.net.Uri
import android.util.Log
import eu.kdvelectronics.upyandroid.managers.SettingsManager
import io.ktor.http.ContentType
import io.ktor.http.HttpStatusCode
import io.ktor.server.application.call
import io.ktor.server.application.install
import io.ktor.server.auth.Authentication
import io.ktor.server.auth.UserIdPrincipal
import io.ktor.server.auth.authenticate
import io.ktor.server.auth.basic
import io.ktor.server.cio.CIO
import io.ktor.server.engine.EmbeddedServer
import io.ktor.server.engine.embeddedServer
import io.ktor.server.plugins.statuspages.StatusPages
import io.ktor.server.response.respond
import io.ktor.server.response.respondSource
import io.ktor.server.response.respondText
import io.ktor.server.routing.get
import io.ktor.server.routing.routing
import kotlinx.io.asSource
import java.io.File
import java.io.FileNotFoundException

// Runs in the default/UI process (same process as AdbExecProvider and,
// eventually, SSH -- see the plan's own Part 7 design), NOT :engine.
// Serves two things, both gated behind http_server_enabled:
// - GET /media, GET /media/{token}: android.mediastore-saved images,
//   streamed straight from their real content:// URI via
//   ContentResolver.openInputStream() -- no intermediate copy. Token-
//   based, not by MediaStore's own sequential _id -- see
//   MediaStoreTokenRegistry's own header comment.
// - GET /files/{path...}: the same VFS root Explorer/Import/Export
//   already expose, gated behind the separate, narrower
//   http_private_files_enabled sub-toggle (checked live, per request,
//   not baked in at server-start time -- flipping it off takes effect
//   on the very next request, no restart needed).
// Same-LAN-only v1 scope (plain HTTP, no relay/tunnel/NAT traversal) --
// see the plan's own Part 7 scope note.
class HttpServerManager(private val context: Context, private val settingsManager: SettingsManager) {
    companion object {
        private const val TAG = "HttpServerManager"
        const val PORT = 8080
    }

    @Volatile
    private var server: EmbeddedServer<*, *>? = null

    // Called once at app start and again after every settings change
    // (same call site as BoardManager.pushSettings() -- see
    // MainActivity.kt) -- starts/stops the whole server in response to
    // http_server_enabled flipping; http_password/
    // http_private_files_enabled are read live per-request instead
    // (see the route handlers below), so they never need a restart.
    @Synchronized
    fun applySettings() {
        val shouldRun = settingsManager.httpServerEnabled
        val running = server != null
        if (shouldRun && !running) {
            start()
        } else if (!shouldRun && running) {
            stop()
        }
    }

    private fun start() {
        Log.i(TAG, "starting HTTP server on port $PORT")
        server = embeddedServer(CIO, port = PORT) {
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
                basic("http-password") {
                    realm = "upy-android"
                    validate { credentials ->
                        // Empty http_password means "not configured" --
                        // deny every request rather than treat it as
                        // "no auth required". Matches this project's
                        // own fail-closed default posture (see
                        // AdbExecProvider.kt's UID check).
                        val configured = settingsManager.httpPassword
                        if (configured.isNotEmpty() && credentials.password == configured) {
                            UserIdPrincipal(credentials.name)
                        } else {
                            null
                        }
                    }
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
                        // URI, no intermediate copy -- see this file's
                        // own header comment) and closes it once fully
                        // consumed; not wrapped in .use{} here, which
                        // would close it before Ktor finishes reading.
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
                        // comment on this same javadoc guarantee) -- a
                        // request path can't escape filesDir via a
                        // leading "/", only via "..", checked next.
                        if (relativePath.split('/').any { it == ".." }) {
                            return@get call.respond(HttpStatusCode.BadRequest)
                        }
                        val file = File(context.filesDir, relativePath)
                        if (!file.isFile) {
                            return@get call.respond(HttpStatusCode.NotFound)
                        }
                        // No MIME sniffing/extension mapping for v1 --
                        // the consumer already knows what it asked for.
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
