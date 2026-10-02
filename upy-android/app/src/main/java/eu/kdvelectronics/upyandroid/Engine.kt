package eu.kdvelectronics.upyandroid

import android.content.Context
import android.view.Surface

// Raw JNI surface. Must only be called from EngineWorker's single worker
// thread. nativeInterrupt() and nativeSetDisplaySurface() are the
// exceptions, safe from any thread; see their own comments in
// engine_jni.cpp.
object Engine {
    init {
        System.loadLibrary("upy_engine")
    }

    // rootPath: app-private storage dir (Context.filesDir.absolutePath),
    // mounted as a jailed VfsPosix at "/". heapSizeMb is fixed for this
    // :engine process's entire lifetime. Changing it needs a real app
    // restart, not just Reset. See EngineWorker.kt#start. applicationContext:
    // needed by mediastore_module.cpp's own MediaStore/ContentResolver
    // access, read once here, same tier as rootPath/heapSizeMb, never
    // re-passed on nativeReset().
    external fun nativeInit(stackSizeBytes: Int, heapSizeMb: Int, rootPath: String, applicationContext: Context): Boolean

    // sink: optional, invoked synchronously on this same call/thread once
    // per print()/traceback write during execution. See EngineOutputSink
    // and engine_jni.cpp's chunk_cb_trampoline. Pass null for the old
    // behavior: accumulated output only, on return.
    external fun nativeExec(code: String, sink: EngineOutputSink?): String
    external fun nativeInterrupt()
    external fun nativeReset(stackSizeBytes: Int, rootPath: String)
    external fun nativeDeinit()

    // Safe from any thread (a Binder thread, in practice). Converts the
    // surface to an ANativeWindow* synchronously on the calling thread:
    // ANativeWindow_fromSurface needs a valid JNIEnv and the real
    // jobject, not a reference queued for later use on a different
    // thread. Hands the resulting plain pointer to display_module.cpp
    // under its own lock. Pass null to detach.
    external fun nativeSetDisplaySurface(surface: Surface?)

    // Writes directly into a mutex-protected native struct on the
    // calling thread. Safe from any thread, same as
    // nativeSetDisplaySurface. No heapSizeMb parameter here: that is
    // read once, in nativeInit() only. See this file's own comment on
    // nativeInit and engine_jni.cpp#nativeSetSettings for why.
    external fun nativeSetSettings(
        sshEnabled: Boolean,
        httpServerEnabled: Boolean,
        httpPrivateFilesEnabled: Boolean,
        adbExecEnabled: Boolean,
    )
}
