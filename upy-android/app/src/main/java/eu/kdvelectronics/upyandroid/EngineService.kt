package eu.kdvelectronics.upyandroid

import android.app.Service
import android.content.Intent
import android.os.Bundle
import android.os.IBinder
import android.os.RemoteException
import android.view.Surface
import eu.kdvelectronics.upyandroid.camera.CameraShim
import eu.kdvelectronics.upyandroid.location.LocationShim
import eu.kdvelectronics.upyandroid.managers.SettingsManager

// Runs in the :engine process (android:process=":engine" in the
// manifest). A native fault here does not take down the main app
// process. Bind-only: no startService or foreground service. Android
// destroys this Service once the last client unbinds, and interpreter
// state is expected to reset then. This is the idle/lazy tier of the
// two-tier reset design; this class does not need to implement reset
// itself.
// Exception: ScriptExecCore's one shared connection (Terminal,
// Explorer/Editor Run, adb-exec, SSH) never unbinds, for the process's
// entire lifetime, not just after adb-exec or SSH has been used. Not a
// leak. See ScriptExecCore.kt#ScriptExecCore.
// see session-state: EngineService.kt#EngineService
// see session-state: IEngine.aidl#reset
class EngineService : Service() {
    // Holds the fileprovider share-request listener as a static, not an
    // instance field: android.fileprovider's native bridge (running on
    // :engine's worker thread) reaches it via FindClass+GetStaticMethodID
    // on this class, the same way engine_jni.cpp's other JNI bridges
    // reach a known class. There being only one EngineService instance
    // alive per process makes this equivalent to an instance field in
    // practice. @JvmStatic is required for the method to compile to a
    // real static method JNI can find, not a Companion-instance method.
    // see session-state: EngineService.kt#Companion
    companion object {
        @Volatile
        private var shareListener: IEngineShareListener? = null

        @JvmStatic
        fun requestShare(path: String, mimeType: String) {
            try {
                shareListener?.onShareRequest(path, mimeType)
            } catch (e: RemoteException) {
                // Main process is gone or unresponsive. Drop the
                // request; the script's share() call still returns
                // normally either way (fire-and-forget, matching the
                // oneway AIDL interface).
            }
        }

        // Asks the main process to show the permission prompt. Same
        // fire-and-forget reasoning as requestShare(). Used by LocationShim.
        @JvmStatic
        fun requestPermissions(permissions: Array<String>) {
            try {
                shareListener?.onPermissionRequest(permissions)
            } catch (e: RemoteException) {
                // Main process is gone; the caller already reports the
                // missing permission to the script.
            }
        }

        // Screen lock for an open camera, -1 for none. Kept so a
        // reconnecting UI gets it again (setShareListener()).
        @Volatile
        private var cameraRotation = -1

        @JvmStatic
        fun setCameraRotation(rotation: Int) {
            cameraRotation = rotation
            sendCameraRotation()
        }

        private fun sendCameraRotation() {
            try {
                shareListener?.onCameraOrientation(cameraRotation)
            } catch (e: RemoteException) {
                // Main process is gone; a new one gets it on connect.
            }
        }
    }

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
        // Plain synchronous local read. No AIDL round-trip needed.
        // heap_size_mb is fixed for this process's whole lifetime;
        // see EngineWorker.kt#start.
        val heapSizeMb = SettingsManager(applicationContext).heapSizeMb
        worker = EngineWorker(filesDir.absolutePath, heapSizeMb, applicationContext)
        worker.start()
        LocationShim.init(applicationContext)
        CameraShim.init(applicationContext)
    }

    private val binder = object : IEngine.Stub() {
        override fun exec(code: String): Bundle {
            val result = worker.exec(code) { chunk ->
                try {
                    outputListener?.onOutputChunk(chunk)
                } catch (e: RemoteException) {
                    // Main process is gone or unresponsive mid-script. Drop
                    // the chunk and keep the script running; exec()'s own
                    // return value on completion is unaffected.
                }
            }
            return Bundle().apply {
                putString("output", result.output)
                putString("exception", result.exception)
            }
        }

        override fun interrupt() = worker.interrupt()
        // A reset ends the script, so its location updates end too.
        override fun reset() {
            LocationShim.stop()
            worker.reset()
        }

        override fun setOutputListener(listener: IEngineOutputListener?) {
            outputListener = listener
        }

        override fun setShareListener(listener: IEngineShareListener?) {
            shareListener = listener
            sendCameraRotation()
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
            // No heap_size_mb here. That is read once, locally, in
            // onCreate() above. See Engine.kt#nativeSetSettings.
            Engine.nativeSetSettings(
                settings.getBoolean("ssh_enabled", false),
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
        CameraShim.closeOnMain()
        worker.deinit()
        super.onDestroy()
    }
}
