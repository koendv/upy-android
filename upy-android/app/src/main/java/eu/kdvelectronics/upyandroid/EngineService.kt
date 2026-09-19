package eu.kdvelectronics.upyandroid

import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.os.RemoteException
import android.view.Surface

// Runs in the :engine process (android:process=":engine" in the
// manifest). A native fault here does not take down the main app
// process. Bind-only: no startService or foreground service. Android
// destroys this Service once the last client unbinds, and interpreter
// state is expected to reset then. This is the idle/lazy tier of the
// two-tier reset design; this class does not need to implement reset
// itself.
// see session-state: IEngine.aidl#reset
class EngineService : Service() {
    // Constructed in onCreate(), not as a property initializer. filesDir,
    // like other Context methods, is not safely available during a
    // Service's own constructor, only from onCreate() onward.
    private lateinit var worker: EngineWorker

    // Set and cleared via setOutputListener() below. Written from a
    // Binder thread pool thread, read from the worker thread inside
    // exec().
    @Volatile
    private var outputListener: IEngineOutputListener? = null

    override fun onCreate() {
        super.onCreate()
        worker = EngineWorker(filesDir.absolutePath)
        worker.start()
    }

    private val binder = object : IEngine.Stub() {
        override fun exec(code: String): String = worker.exec(code) { chunk ->
            try {
                outputListener?.onOutputChunk(chunk)
            } catch (e: RemoteException) {
                // Main process is gone or unresponsive mid-script. Drop
                // the chunk and keep the script running; exec()'s own
                // return value on completion is unaffected.
            }
        }

        override fun interrupt() = worker.interrupt()
        override fun reset() = worker.reset()

        override fun setOutputListener(listener: IEngineOutputListener?) {
            outputListener = listener
        }

        // Deliberately not queued through worker.taskQueue like every
        // other call here. This touches no MicroPython or GC state (the
        // queue exists to serialize access to that), only
        // display_module.cpp's own mutex-protected ANativeWindow*.
        // Called directly on this Binder thread, which is what
        // ANativeWindow_fromSurface() needs. See Engine.kt.
        override fun setDisplaySurface(surface: Surface?) {
            Engine.nativeSetDisplaySurface(surface)
        }
    }

    override fun onBind(intent: Intent): IBinder = binder

    // Defense-in-depth camera teardown. Idle-unbind should already get
    // equivalent cleanup for free via process death, but this removes
    // reliance on that assumption.
    // see session-state: EngineService.kt#onDestroy
    override fun onDestroy() {
        worker.deinit()
        super.onDestroy()
    }
}
