package eu.kdvelectronics.upyandroid.managers

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Bundle
import android.os.IBinder
import android.os.RemoteException
import android.util.Log
import android.view.Surface
import eu.kdvelectronics.upyandroid.EngineService
import eu.kdvelectronics.upyandroid.IEngine
import eu.kdvelectronics.upyandroid.IEngineOutputListener
import eu.kdvelectronics.upyandroid.IEngineShareListener
import eu.kdvelectronics.upyandroid.model.ConnectionStatus

/**
 * The local transport: binds to [EngineService] (running in the separate
 * `:engine` process) over AIDL and exposes exec()/interrupt()/reset().
 * Backed by Binder, with typed call/return, not a raw-REPL byte protocol.
 *
 * Reconnection is not assumed automatic, and this class never retries on
 * its own -- callers (the UI, ScriptExecCore's ensureConnected()) decide
 * when to call [connect] again. Whether the OS actually restores the
 * binding on its own after a crashed :engine process is device-
 * dependent: MIUI is documented to block auto-restart of a killed bound
 * service, requiring an explicit reconnect; a stock-ish Samsung tablet
 * (SM-T500, Android, confirmed directly by killing :engine) instead
 * auto-restarted the service and re-delivered onServiceConnected with no
 * explicit [connect] call at all. Don't assume either behavior; always
 * go through the explicit reconnect path.
 */
class BoardManager(
    private val context: Context,
    private val onStatusChanges: ((status: ConnectionStatus) -> Unit)? = null,
) {
    companion object {
        private const val TAG = "BoardManager"
    }

    @Volatile
    private var engine: IEngine? = null

    // Whether bindService() below currently has an outstanding bind.
    // Needed for connect()'s own stuck-connection recovery -- see there.
    @Volatile
    private var bound = false

    // Kept separate from the AIDL Stub below so callers can register or
    // replace it independent of the bind lifecycle. Re-applied to the
    // engine on every reconnect, since a crashed :engine process starts
    // a fresh EngineService with no listener registered.
    @Volatile
    private var chunkListener: ((String) -> Unit)? = null

    private val outputListenerStub = object : IEngineOutputListener.Stub() {
        override fun onOutputChunk(text: String) {
            chunkListener?.invoke(text)
        }
    }

    // Same single-slot reasoning as chunkListener above: EngineService
    // holds exactly one share-listener registration, last one wins.
    // Only one BoardManager instance exists for the whole process (see
    // ScriptExecCore.kt#ScriptExecCore), so there is nothing left to
    // collide with.
    @Volatile
    private var shareRequestListener: ((path: String, mimeType: String) -> Unit)? = null

    @Volatile
    private var permissionRequestListener: ((permissions: Array<String>) -> Unit)? = null

    private val shareListenerStub = object : IEngineShareListener.Stub() {
        override fun onShareRequest(path: String, mimeType: String) {
            shareRequestListener?.invoke(path, mimeType)
        }

        override fun onPermissionRequest(permissions: Array<String>) {
            permissionRequestListener?.invoke(permissions)
        }
    }

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            engine = IEngine.Stub.asInterface(binder)
            // Before Connected, unconditionally. See
            // IEngine.aidl#setSettings for why this ordering matters.
            pushSettings()
            engine?.setOutputListener(outputListenerStub)
            engine?.setShareListener(shareListenerStub)
            onStatusChanges?.invoke(ConnectionStatus.Connected)
        }

        override fun onServiceDisconnected(name: ComponentName) {
            // Crash isolation payoff: a fault in the :engine process
            // surfaces here, not as an app crash.
            Log.w(TAG, "engine process disconnected")
            engine = null
            onStatusChanges?.invoke(ConnectionStatus.Disconnected("engine process disconnected"))
        }
    }

    fun connect() {
        // REAL BUG, confirmed on-device (2026-09-29): a redundant
        // connect() call while already genuinely connected (bound=true,
        // engine != null -- e.g. MainActivity.onCreate() running again
        // after a process restart, or the user tapping Reconnect while
        // already connected) used to fall through to bindService()
        // unconditionally below, same as every other case. Android does
        // NOT redeliver onServiceConnected for a connection it already
        // considers bound (see the stuck-connection-recovery comment
        // below), so the Connecting status set at the top of this
        // function never got followed by a matching Connected --
        // status stayed permanently stuck at Connecting (which the UI
        // shows identically to Disconnected) while the real connection,
        // and any script currently running on it, kept working
        // completely unaffected. Confirmed via `dumpsys activity
        // services`: two ConnectionRecords ended up bound to the same
        // EngineService instance after this happened once. This early
        // return is the fix: a truly-already-connected call is a
        // real no-op, not a reconnect attempt.
        if (bound && engine != null) {
            onStatusChanges?.invoke(ConnectionStatus.Connected)
            return
        }
        onStatusChanges?.invoke(ConnectionStatus.Connecting)
        // Stuck-connection recovery, a real bug hit and confirmed
        // on-device: if bound is already true but engine is still null,
        // this app's own onServiceConnected was never observed for the
        // current binding (e.g. a missed/raced auto-reconnect after
        // :engine died while this process was backgrounded), yet
        // Android's own ActivityManager still considers the
        // ServiceConnection bound (confirmed via dumpsys activity
        // services: hasBound=true, multiple stacked ConnectionRecords
        // on the same BinderProxy). bindService() on a connection
        // Android already considers bound does NOT re-deliver
        // onServiceConnected -- confirmed by a run() call that waited
        // the full 10s ensureConnected() timeout and still got
        // Disconnected. unbindService() first forces Android to drop
        // the stale connection, so the bindService() below is
        // guaranteed a fresh connection cycle.
        if (bound && engine == null) {
            context.unbindService(connection)
            bound = false
        }
        // BIND_ABOVE_CLIENT keeps :engine's process importance tied to
        // at least this client process's own, instead of the OS's
        // default demotion while bound. Needed for reliable sensor
        // delivery.
        // see session-state: BoardManager.kt#connect
        val ok = context.bindService(
            Intent(context, EngineService::class.java),
            connection,
            Context.BIND_AUTO_CREATE or Context.BIND_ABOVE_CLIENT
        )
        bound = ok
        if (!ok) {
            onStatusChanges?.invoke(ConnectionStatus.Disconnected("could not bind engine service"))
        }
    }

    /**
     * Pushes the current, non-secret settings snapshot into :engine.
     * Called on every successful connect (before onStatusChanges sees
     * Connected, see [onServiceConnected]) and again from the
     * Settings screen whenever the user changes a setting while
     * connected. sshPassword/httpPassword are deliberately never put
     * into this Bundle, see IEngine.aidl#setSettings. heap_size_mb is
     * deliberately not here either. That is read once, locally, by
     * EngineService.onCreate() only; see Engine.kt#nativeSetSettings.
     */
    fun pushSettings() {
        val s = SettingsManager(context)
        val bundle = Bundle().apply {
            putBoolean("ssh_enabled", s.sshEnabled)
            putBoolean("http_server_enabled", s.httpServerEnabled)
            putBoolean("http_private_files_enabled", s.httpPrivateFilesEnabled)
            putBoolean("litert_playstore_enabled", s.litertPlaystoreEnabled)
            putBoolean("adb_exec_enabled", s.adbExecEnabled)
        }
        try {
            engine?.setSettings(bundle)
        } catch (re: RemoteException) {
            // Engine is gone; the next connect() will push a fresh
            // snapshot into whatever process replaces it.
        }
    }

    /**
     * Registers a callback for incremental print()/traceback output
     * produced while a script is running (e.g. a `while True: print(...)`
     * loop), delivered on a Binder thread pool thread, never the
     * caller's own thread. Pass null to stop listening. Independent of
     * exec()'s own return value, which still carries the full
     * accumulated output on completion.
     */
    fun setOutputListener(listener: ((String) -> Unit)?) {
        chunkListener = listener
    }

    /**
     * Registers a callback for android.fileprovider.share() requests
     * raised from a running script (path + MIME type of the file to
     * share), delivered on a Binder thread pool thread. Pass null to
     * stop listening.
     */
    fun setShareRequestListener(listener: ((path: String, mimeType: String) -> Unit)?) {
        shareRequestListener = listener
    }

    fun setPermissionRequestListener(listener: ((permissions: Array<String>) -> Unit)?) {
        permissionRequestListener = listener
    }

    /**
     * Blocks until the code has finished executing; call off the UI
     * thread. "Not connected" and crashed-mid-call cases are reported
     * via onStatusChanges, like any other disconnect, not smuggled into
     * this return value. exec()'s output arrives live via the output
     * listener; this return value is not used to display it.
     */
    fun exec(code: String): String {
        val e = engine
        if (e == null) {
            onStatusChanges?.invoke(ConnectionStatus.Disconnected("not connected to engine"))
            return ""
        }
        return try {
            e.exec(code)
        } catch (re: RemoteException) {
            onStatusChanges?.invoke(ConnectionStatus.Disconnected("engine process disconnected during execution"))
            ""
        }
    }

    fun interrupt() {
        engine?.interrupt()
    }

    fun reset() {
        engine?.reset()
    }

    /**
     * Hands :engine the fourth screen's SurfaceView Surface, or null
     * when it is torn down (navigated away, backgrounded). Called from
     * SurfaceHolder.Callback, so a RemoteException here (the engine
     * crashed exactly during a surface attach or detach) must not
     * propagate and crash the main process. Best-effort, silently
     * dropped, like exec()'s own RemoteException handling.
     */
    fun setDisplaySurface(surface: Surface?) {
        try {
            engine?.setDisplaySurface(surface)
        } catch (re: RemoteException) {
            // Engine is gone; nothing to attach to.
        }
    }
}
