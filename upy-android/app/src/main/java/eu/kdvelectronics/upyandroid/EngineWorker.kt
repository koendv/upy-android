package eu.kdvelectronics.upyandroid

import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue

// The single reused background worker thread -- created once, all
// exec()/reset() calls are serialized through it, matching the
// architecture decision (project memory: "a single background worker
// thread, created once and reused across all eval calls"). interrupt() is
// the one operation that bypasses the queue, since it must be able to
// reach a script that's currently blocking the worker thread.
// rootPath: app-private storage dir (Context.filesDir.absolutePath),
// mounted as a jailed VfsPosix at "/" -- see project memory. Same physical
// path regardless of which process reads it (same app/UID), even though
// this class runs in the :engine process via EngineService's own Context.
class EngineWorker(private val rootPath: String) {
    companion object {
        // Passed into nativeInit/nativeReset so mp_cstack_init_with_top()
        // gets this thread's real stack size -- see engine_jni.cpp.
        private const val STACK_SIZE_BYTES = 512 * 1024
    }

    private val taskQueue = LinkedBlockingQueue<Runnable>()
    private lateinit var thread: Thread

    fun start() {
        thread = Thread(null, {
            Engine.nativeInit(STACK_SIZE_BYTES, rootPath)
            while (true) {
                taskQueue.take().run()
            }
        }, "mp-engine-worker", STACK_SIZE_BYTES.toLong())
        thread.isDaemon = true
        thread.start()
    }

    // onChunk fires synchronously on this worker thread, once per
    // print()/traceback write, before exec() itself returns -- see
    // EngineOutputSink / engine_jni.cpp.
    fun exec(code: String, onChunk: (String) -> Unit = {}): String {
        val latch = CountDownLatch(1)
        var result = ""
        taskQueue.put {
            val sink = EngineOutputSink { chunk -> onChunk(chunk) }
            result = Engine.nativeExec(code, sink)
            latch.countDown()
        }
        latch.await()
        return result
    }

    // Safe from any calling thread -- see engine_jni.cpp's nativeInterrupt.
    fun interrupt() {
        Engine.nativeInterrupt()
    }

    fun reset() {
        val latch = CountDownLatch(1)
        taskQueue.put {
            Engine.nativeReset(STACK_SIZE_BYTES, rootPath)
            latch.countDown()
        }
        latch.await()
    }

    // Called from EngineService.onDestroy() -- defense-in-depth camera
    // teardown, see engine_jni.cpp's nativeDeinit(). Queued like reset(),
    // not called directly from the caller's own thread, since
    // nativeDeinit() touches the same interpreter/native state every
    // other queued call does (unlike interrupt(), which is deliberately
    // thread-safe from anywhere).
    fun deinit() {
        val latch = CountDownLatch(1)
        taskQueue.put {
            Engine.nativeDeinit()
            latch.countDown()
        }
        latch.await()
    }
}
