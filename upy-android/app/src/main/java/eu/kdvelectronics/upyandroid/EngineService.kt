package eu.kdvelectronics.upyandroid

import android.app.Service
import android.content.Intent
import android.os.Bundle
import android.os.IBinder
import android.os.RemoteException
import android.view.Surface
import eu.kdvelectronics.upyandroid.managers.SettingsManager

// Runs in the :engine process (android:process=":engine" in the
// manifest). A native fault here does not take down the main app
// process. Bind-only: no startService or foreground service. Android
// destroys this Service once the last client unbinds, and interpreter
// state is expected to reset then. This is the idle/lazy tier of the
// two-tier reset design; this class does not need to implement reset
// itself.
// Exception: AdbExecProvider never unbinds. Not a leak.
// see session-state: EngineService.kt#EngineService
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
        // Plain synchronous local read -- no AIDL round-trip needed.
        // heap_size_mb is fixed for this process's whole lifetime;
        // see EngineWorker.kt#start.
        val heapSizeMb = SettingsManager(applicationContext).heapSizeMb
        worker = EngineWorker(filesDir.absolutePath, heapSizeMb, applicationContext)
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

        // Deliberately not queued, same reasoning as setDisplaySurface
        // above: touches only a mutex-protected native struct, not
        // MicroPython/GC state, so it must not wait behind a running
        // script. See IEngine.aidl#setSettings.
        override fun setSettings(settings: Bundle) {
            // No heap_size_mb here -- that is read once, locally, in
            // onCreate() above. See Engine.kt#nativeSetSettings.
            Engine.nativeSetSettings(
                settings.getBoolean("ssh_enabled", false),
                settings.getBoolean("http_server_enabled", false),
                settings.getBoolean("http_private_files_enabled", false),
                settings.getBoolean("litert_playstore_enabled", false),
                settings.getBoolean("adb_exec_enabled", false),
            )
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
