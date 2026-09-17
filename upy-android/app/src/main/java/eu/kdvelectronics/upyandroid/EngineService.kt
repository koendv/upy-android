package eu.kdvelectronics.upyandroid

import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.os.RemoteException
import android.view.Surface

// Runs in the :engine process (android:process=":engine" in the
// manifest) -- a native fault here does not take down the main app
// process. Bind-only (no startService/foreground service): Android
// destroys this Service once the last client unbinds, and interpreter
// state is expected to reset then -- that's the idle/lazy tier of the
// two-tier reset design, not something this class needs to implement.
class EngineService : Service() {
    // Constructed in onCreate(), not as a property initializer -- filesDir
    // (like other Context methods) isn't safely available during a
    // Service's own constructor, only from onCreate() onward.
    private lateinit var worker: EngineWorker

    // Set/cleared via setOutputListener() below -- written from a Binder
    // thread pool thread, read from the worker thread inside exec().
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
                // Main process gone/unresponsive mid-script -- drop the
                // chunk and keep the script running. exec()'s own return
                // value on completion is unaffected.
            }
        }

        override fun interrupt() = worker.interrupt()
        override fun reset() = worker.reset()

        override fun setOutputListener(listener: IEngineOutputListener?) {
            outputListener = listener
        }

        // Deliberately NOT queued through worker.taskQueue like every
        // other call here -- this touches no MicroPython/GC state (the
        // queue exists to serialize access to that), only
        // display_module.cpp's own mutex-protected ANativeWindow*. Called
        // directly on this Binder thread, which is what
        // ANativeWindow_fromSurface() needs (see Engine.kt).
        override fun setDisplaySurface(surface: Surface?) {
            Engine.nativeSetDisplaySurface(surface)
        }
    }

    override fun onBind(intent: Intent): IBinder = binder

    // Defense-in-depth camera teardown (see SESSION_STATE.yaml's camera
    // teardown hooks decision) -- idle-unbind should already get
    // equivalent camera-service-side cleanup for free via process death,
    // same mechanism proven for crash-kill, but this removes reliance on
    // that assumption rather than leaving it unverified.
    override fun onDestroy() {
        worker.deinit()
        super.onDestroy()
    }
}
