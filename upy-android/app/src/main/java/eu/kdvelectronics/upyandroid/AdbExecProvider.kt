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
import androidx.core.content.pm.PackageInfoCompat
import eu.kdvelectronics.upyandroid.managers.SettingsManager
import eu.kdvelectronics.upyandroid.model.ConnectionStatus

// adb-driven one-shot MicroPython exec. Delegates the actual
// connect/run/reset/interrupt work to ScriptExecCore, shared with the
// SSH shell. This class is now only the UID-gated, base64-decoding
// front door.
// see session-state: AdbExecProvider.kt#AdbExecProvider
class AdbExecProvider : ContentProvider() {
    private lateinit var appContext: Context

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
            return Bundle().apply { putString("error", "disabled - enable adb exec in app settings") }
        }

        return when (method) {
            "help" -> handleHelp()
            "status" -> handleStatus()
            "run" -> handleRun(arg)
            "reset" -> {
                ScriptExecCore.reset(appContext)
                Bundle()
            }
            "interrupt" -> {
                ScriptExecCore.interrupt(appContext)
                Bundle()
            }
            else -> Bundle().apply { putString("error", "unknown_method - try --method help") }
        }
    }

    // Static, AI-oriented description of this interface. Never touches
    // the engine, so it answers even while a script runs.
    private fun handleHelp(): Bundle {
        val text = appContext.resources.openRawResource(R.raw.adb_help)
            .bufferedReader().use { it.readText() }
        return Bundle().apply { putString("output", text) }
    }

    // Non-blocking snapshot: no ensureConnected(), no engine call.
    private fun handleStatus(): Bundle {
        val info = appContext.packageManager.getPackageInfo(appContext.packageName, 0)
        val connected = ScriptExecCore.status.value is ConnectionStatus.Connected
        val text = "protocol: $PROTOCOL_VERSION\n" +
            "app: ${info.versionName} (${PackageInfoCompat.getLongVersionCode(info)})\n" +
            "engine: ${if (connected) "connected" else "disconnected"}\n" +
            "busy: ${if (ScriptExecCore.busy) "yes" else "no"}\n"
        return Bundle().apply { putString("output", text) }
    }

    // see session-state: AdbExecProvider.kt#handleRun
    private fun handleRun(arg: String?): Bundle {
        if (arg.isNullOrEmpty()) {
            return Bundle().apply { putString("error", "missing_arg - run needs --arg <base64 script>") }
        }
        val decoded = try {
            String(Base64.decode(arg, Base64.DEFAULT), Charsets.UTF_8)
        } catch (e: IllegalArgumentException) {
            return Bundle().apply { putString("error", "bad_base64") }
        }
        // Header written by run() itself, once the busy check succeeds
        // -- see ScriptExecCore.kt#ScriptExecCore. Live output already
        // reaches TerminalLog via the shared connection's output
        // listener as the script runs; this return value only needs to
        // go back to the adb caller.
        return when (val result = ScriptExecCore.run(appContext, decoded, label = "adb-exec")) {
            is ScriptExecCore.RunResult.Busy -> Bundle().apply { putString("error", "busy") }
            is ScriptExecCore.RunResult.Disconnected -> Bundle().apply { putString("error", "disconnected") }
            is ScriptExecCore.RunResult.Ok -> Bundle().apply { putString("output", result.output) }
        }
    }

    companion object {
        // Bump on any change to methods, args or reply format. Also in
        // res/raw/adb_help.yaml.
        const val PROTOCOL_VERSION = 1
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
