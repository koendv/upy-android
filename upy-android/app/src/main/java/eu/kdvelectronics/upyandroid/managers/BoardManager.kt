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
import eu.kdvelectronics.upyandroid.model.ExecResult

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

    @Volatile
    private var inputWaitingListener: ((Boolean) -> Unit)? = null

    private val outputListenerStub = object : IEngineOutputListener.Stub() {
        override fun onOutputChunk(text: String) {
            chunkListener?.invoke(text)
        }

        override fun onInputWaiting(waiting: Boolean) {
            inputWaitingListener?.invoke(waiting)
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

    @Volatile
    private var cameraRotationListener: ((rotation: Int) -> Unit)? = null

    private val shareListenerStub = object : IEngineShareListener.Stub() {
        override fun onShareRequest(path: String, mimeType: String) {
            shareRequestListener?.invoke(path, mimeType)
        }

        override fun onPermissionRequest(permissions: Array<String>) {
            permissionRequestListener?.invoke(permissions)
        }

        override fun onCameraOrientation(rotation: Int) {
            cameraRotationListener?.invoke(rotation)
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
        // Already connected (e.g. Reconnect tapped, or onCreate() run
        // again): a no-op. Binding again would not deliver
        // onServiceConnected, leaving the status stuck at Connecting.
        if (bound && engine != null) {
            onStatusChanges?.invoke(ConnectionStatus.Connected)
            return
        }
        onStatusChanges?.invoke(ConnectionStatus.Connecting)
        // Bound but onServiceConnected never seen for this binding
        // (e.g. :engine died while this process was in the background):
        // bindService() on a connection Android considers bound does not
        // deliver onServiceConnected again, so unbind first.
        if (bound && engine == null) {
            context.unbindService(connection)
            bound = false
        }
        // BIND_ABOVE_CLIENT keeps :engine's process importance tied to
        // at least this client process's own, instead of the OS's
        // default demotion while bound. Needed for reliable sensor
        // delivery.
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
     * connected. sshPassword is deliberately never put
     * into this Bundle, see IEngine.aidl#setSettings. heap_size_mb is
     * deliberately not here either. That is read once, locally, by
     * EngineService.onCreate() only; see Engine.kt#nativeSetSettings.
     */
    fun pushSettings() {
        val s = SettingsManager(context)
        val bundle = Bundle().apply {
            putBoolean("ssh_enabled", s.sshEnabled)
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

    /** Called when a running script starts or stops waiting for input. */
    fun setInputWaitingListener(listener: ((Boolean) -> Unit)?) {
        inputWaitingListener = listener
    }

    /**
     * Bytes for the running script's sys.stdin. Returns the number of
     * bytes accepted, or -1 when no script runs or the engine is gone.
     * Not queued behind exec(): call from any thread except the UI thread.
     */
    fun sendInput(data: ByteArray): Int = try {
        engine?.sendInput(data) ?: -1
    } catch (re: RemoteException) {
        -1
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

    fun setCameraRotationListener(listener: ((rotation: Int) -> Unit)?) {
        cameraRotationListener = listener
    }

    /**
     * Blocks until the code has finished executing; call off the UI
     * thread. "Not connected" and crashed-mid-call cases are reported
     * via onStatusChanges, like any other disconnect, not smuggled into
     * this return value. exec()'s output arrives live via the output
     * listener; this return value is not used to display it.
     */
    fun exec(code: String): ExecResult {
        val e = engine
        if (e == null) {
            onStatusChanges?.invoke(ConnectionStatus.Disconnected("not connected to engine"))
            return ExecResult("", "")
        }
        return try {
            val b = e.exec(code)
            ExecResult(b.getString("output", ""), b.getString("exception", ""))
        } catch (re: RemoteException) {
            onStatusChanges?.invoke(ConnectionStatus.Disconnected("engine process disconnected during execution"))
            ExecResult("", "")
        }
    }

    // Silent when the engine is gone: nothing left to interrupt.
    fun interrupt() {
        try {
            engine?.interrupt()
        } catch (re: RemoteException) {
        }
    }

    // False when there is no engine or it died during the reset.
    fun reset(): Boolean {
        val e = engine
        if (e == null) {
            onStatusChanges?.invoke(ConnectionStatus.Disconnected("not connected to engine"))
            return false
        }
        return try {
            e.reset()
            true
        } catch (re: RemoteException) {
            onStatusChanges?.invoke(ConnectionStatus.Disconnected("engine process disconnected during reset"))
            false
        }
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
