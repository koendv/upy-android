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
 * Backed by Binder, with typed call/return, not a raw-REPL byte protocol.
 *
 * Reconnection is not automatic. MIUI blocks auto-restart of a killed
 * bound service, so a crashed engine must be recovered by explicitly
 * calling [connect] again. Callers (the UI) decide when; this class does
 * not retry on its own.
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

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            engine = IEngine.Stub.asInterface(binder)
            engine?.setOutputListener(outputListenerStub)
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
        onStatusChanges?.invoke(ConnectionStatus.Connecting)
        // BIND_ABOVE_CLIENT keeps :engine's process importance tied to
        // at least this client process's own, instead of the OS's
        // default demotion while bound. Needed for reliable sensor
        // delivery.
        // see session-state: BoardManager.kt#connect
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
     * loop), delivered on a Binder thread pool thread, never the
     * caller's own thread. Pass null to stop listening. Independent of
     * exec()'s own return value, which still carries the full
     * accumulated output on completion.
     */
    fun setOutputListener(listener: ((String) -> Unit)?) {
        chunkListener = listener
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
