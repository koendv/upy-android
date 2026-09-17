package eu.kdvelectronics.upyandroid

// Called from native code (engine_jni.cpp's chunk_cb_trampoline) on the
// engine worker thread, once per print()/traceback write during
// nativeExec() -- in addition to the full accumulated string nativeExec()
// still returns once execution finishes. A Kotlin `fun interface` (rather
// than a plain lambda type) so JNI's GetMethodID("onChunk", ...) always
// finds a stable, predictable method name on whatever object a caller
// passes, regardless of how they construct it.
fun interface EngineOutputSink {
    fun onChunk(text: String)
}
