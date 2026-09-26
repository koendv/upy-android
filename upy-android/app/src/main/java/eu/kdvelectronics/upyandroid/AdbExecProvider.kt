package eu.kdvelectronics.upyandroid

import android.content.ContentProvider
import android.content.ContentValues
import android.content.Context
import android.database.Cursor
import android.net.Uri
import android.os.Binder
import android.os.Bundle
import android.os.Process
import android.util.Base64
import eu.kdvelectronics.upyandroid.managers.BoardManager
import eu.kdvelectronics.upyandroid.managers.SettingsManager
import eu.kdvelectronics.upyandroid.model.ConnectionStatus
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

// adb-driven one-shot MicroPython exec. Never unbinds its BoardManager
// once connected -- intentional, not a leak.
// see session-state: AdbExecProvider.kt#AdbExecProvider
class AdbExecProvider : ContentProvider() {
    private lateinit var appContext: Context
    private val connectLock = Any()

    @Volatile private var boardManager: BoardManager? = null
    @Volatile private var connected = false
    @Volatile private var latch: CountDownLatch? = null

    // Only "run" is gated -- "reset"/"interrupt" are not.
    private val busy = AtomicBoolean(false)

    override fun onCreate(): Boolean {
        appContext = context!!.applicationContext
        return true
    }

    override fun call(method: String, arg: String?, extras: Bundle?): Bundle {
        val callingUid = Binder.getCallingUid()
        if (callingUid != Process.SHELL_UID && callingUid != Process.ROOT_UID) {
            throw SecurityException("AdbExecProvider: caller uid $callingUid is not shell/root")
        }

        if (!SettingsManager(appContext).adbExecEnabled) {
            return Bundle().apply { putString("error", "disabled") }
        }

        return when (method) {
            "run" -> handleRun(arg)
            "reset" -> {
                ensureConnected()
                boardManager?.reset()
                Bundle()
            }
            "interrupt" -> {
                // Bypasses the busy gate below (run() only).
                ensureConnected()
                boardManager?.interrupt()
                Bundle()
            }
            else -> Bundle().apply { putString("error", "unknown_method") }
        }
    }

    private fun handleRun(arg: String?): Bundle {
        if (!busy.compareAndSet(false, true)) {
            return Bundle().apply { putString("error", "busy") }
        }
        try {
            val decoded = try {
                String(Base64.decode(arg, Base64.DEFAULT), Charsets.UTF_8)
            } catch (e: IllegalArgumentException) {
                return Bundle().apply { putString("error", "bad_base64") }
            }
            ensureConnected()
            val output = boardManager?.exec(decoded) ?: ""
            // "" is ambiguous (no output vs. engine died); check connected.
            return if (!connected) {
                Bundle().apply { putString("error", "disconnected") }
            } else {
                Bundle().apply { putString("output", output) }
            }
        } finally {
            busy.set(false)
        }
    }

    // see session-state: AdbExecProvider.kt#ensureConnected
    private fun ensureConnected() {
        synchronized(connectLock) {
            if (connected) return

            val bm = boardManager ?: BoardManager(
                context = appContext,
                registerOutputListener = false,
            ) { status ->
                connected = status is ConnectionStatus.Connected
                // Connecting is transient/synchronous; only a terminal
                // status may release the latch.
                if (status !is ConnectionStatus.Connecting) {
                    latch?.countDown()
                }
            }.also { boardManager = it }

            val freshLatch = CountDownLatch(1)
            latch = freshLatch
            bm.connect()
            freshLatch.await(10, TimeUnit.SECONDS)
        }
    }

    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?,
    ): Cursor? = null

    override fun getType(uri: Uri): String? = null

    override fun insert(uri: Uri, values: ContentValues?): Uri? =
        throw UnsupportedOperationException()

    override fun update(
        uri: Uri,
        values: ContentValues?,
        selection: String?,
        selectionArgs: Array<out String>?,
    ): Int = throw UnsupportedOperationException()

    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int =
        throw UnsupportedOperationException()
}
