package eu.kdvelectronics.upyandroid.managers

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import android.os.RemoteException
import android.util.Log
import android.view.Surface
import eu.kdvelectronics.upyandroid.EngineService
import eu.kdvelectronics.upyandroid.IEngine
import eu.kdvelectronics.upyandroid.IEngineOutputListener
import eu.kdvelectronics.upyandroid.model.ConnectionStatus

/**
 * The local transport: binds to [EngineService] (running in the separate
 * `:engine` process) over AIDL and exposes exec()/interrupt()/reset().
 *
 * This replaces micro-repl's original USB-serial `BoardManager` -- same
 * role (the one thing everything else above it talks to), but backed by
 * Binder instead of a UART, and typed call/return instead of the raw-REPL
 * byte protocol (see project memory). USB-serial support is dropped
 * entirely for v1, not kept as a second transport.
 *
 * Reconnection is NOT automatic: a MIUI-specific finding (this session)
 * is that the OS blocks auto-restart of a killed bound service, so a
 * crashed engine must be recovered by explicitly calling [connect] again
 * -- callers (the UI) decide when, this class doesn't retry on its own.
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

    // Kept separate from the AIDL Stub below so callers can register/
    // replace it independent of the bind lifecycle -- re-applied to the
    // engine on every (re)connect, since a crashed :engine process starts
    // a fresh EngineService with no listener registered.
    @Volatile
    private var chunkListener: ((String) -> Unit)? = null

    private val outputListenerStub = object : IEngineOutputListener.Stub() {
        override fun onOutputChunk(text: String) {
            chunkListener?.invoke(text)
        }
    }

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            engine = IEngine.Stub.asInterface(binder)
            engine?.setOutputListener(outputListenerStub)
            onStatusChanges?.invoke(ConnectionStatus.Connected)
        }

        override fun onServiceDisconnected(name: ComponentName) {
            // Crash-isolation payoff (proven working this session): a
            // fault in the :engine process surfaces here, not as an app
            // crash.
            Log.w(TAG, "engine process disconnected")
            engine = null
            onStatusChanges?.invoke(ConnectionStatus.Disconnected("engine process disconnected"))
        }
    }

    fun connect() {
        onStatusChanges?.invoke(ConnectionStatus.Connecting)
        // BIND_ABOVE_CLIENT (added 2026-09-16, see SESSION_STATE.yaml's
        // imu.py real-bug entry): plain BIND_AUTO_CREATE left :engine's
        // process importance classified as "cached" (confirmed via
        // `adb shell dumpsys activity processes`) even while the main UI
        // was actively foreground -- Android's power management batches/
        // throttles non-wakeup sensor delivery (imu_module.cpp) much more
        // aggressively for cached processes, which is what caused
        // reproducible ETIMEDOUT errors on real hardware. BIND_ABOVE_CLIENT
        // keeps :engine's importance tied to (at least as high as) this
        // client process's own, instead of the OS's default demotion.
        val bound = context.bindService(
            Intent(context, EngineService::class.java),
            connection,
            Context.BIND_AUTO_CREATE or Context.BIND_ABOVE_CLIENT
        )
        if (!bound) {
            onStatusChanges?.invoke(ConnectionStatus.Disconnected("could not bind engine service"))
        }
    }

    fun disconnect() {
        engine = null
        context.unbindService(connection)
    }

    /**
     * Registers a callback for incremental print()/traceback output
     * produced while a script is running (e.g. a `while True: print(...)`
     * loop), delivered on a Binder thread pool thread -- never the
     * caller's own thread. Pass null to stop listening. Independent of
     * exec()'s own return value, which still carries the full accumulated
     * output on completion as before.
     */
    fun setOutputListener(listener: ((String) -> Unit)?) {
        chunkListener = listener
    }

    /**
     * Blocks until the code has finished executing -- call off the UI
     * thread. The "not connected"/crashed-mid-call cases are reported via
     * onStatusChanges (like any other disconnect), not smuggled into this
     * return value, since exec()'s own output is now expected to arrive
     * live via the output listener rather than by re-displaying this
     * return value.
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
     * Hands :engine the fourth screen's SurfaceView Surface (or null when
     * it's torn down -- navigated away, backgrounded). Called from
     * SurfaceHolder.Callback, so a RemoteException here (engine crashed
     * exactly during a surface attach/detach) must not propagate and
     * crash the main process -- best-effort, silently dropped like
     * exec()'s own RemoteException handling.
     */
    fun setDisplaySurface(surface: Surface?) {
        try {
            engine?.setDisplaySurface(surface)
        } catch (re: RemoteException) {
            // Engine gone -- nothing to attach to anyway.
        }
    }
}
