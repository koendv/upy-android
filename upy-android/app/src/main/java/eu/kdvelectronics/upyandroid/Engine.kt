package eu.kdvelectronics.upyandroid

import android.view.Surface

// Raw JNI surface. Must only be called from EngineWorker's single worker
// thread (nativeInterrupt and nativeSetDisplaySurface are the exceptions
// -- safe from any thread; see their own doc comments in engine_jni.cpp).
object Engine {
    init {
        System.loadLibrary("upy_engine")
    }

    // rootPath: app-private storage dir (Context.filesDir.absolutePath),
    // mounted as a jailed VfsPosix at "/" -- see project memory.
    external fun nativeInit(stackSizeBytes: Int, rootPath: String): Boolean

    // sink: optional, invoked synchronously on this same call/thread once
    // per print()/traceback write during execution -- see
    // EngineOutputSink and engine_jni.cpp's chunk_cb_trampoline. Pass null
    // for the old behavior (accumulated output only, on return).
    external fun nativeExec(code: String, sink: EngineOutputSink?): String
    external fun nativeInterrupt()
    external fun nativeReset(stackSizeBytes: Int, rootPath: String)
    external fun nativeDeinit()

    // Safe from any thread (a Binder thread, in practice) -- converts
    // surface to an ANativeWindow* synchronously on the calling thread
    // (ANativeWindow_fromSurface needs a valid JNIEnv + the real jobject,
    // not a reference queued for later use on a different thread), then
    // hands the resulting plain pointer to display_module.cpp under its
    // own lock. Pass null to detach.
    external fun nativeSetDisplaySurface(surface: Surface?)
}
