package eu.kdvelectronics.upyandroid

import android.content.Context
import eu.kdvelectronics.upyandroid.managers.BoardManager
import eu.kdvelectronics.upyandroid.model.ConnectionStatus
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

// Shared connect-and-wait/busy-gate/BoardManager-holding core for every
// "submit a script, get its output back" caller in the default/UI
// process -- AdbExecProvider (Part 1) and the SSH shell (Part 8,
// SshServerManager.kt) both delegate here instead of each keeping their
// own BoardManager instance and busy flag. Real, deliberate
// consequence: a script running via adb-exec correctly shows "busy" to
// a concurrent SSH session and vice versa, since both share the exact
// same underlying :engine connection and worker-thread queue anyway --
// two independent connections would not have been more correct, just
// two BoardManager objects pointed at the same single :engine process.
//
// Never registers as the output listener (registerOutputListener =
// false, see BoardManager.kt) -- this is not MainActivity's own
// primary/UI BoardManager, so it must not steal that listener slot.
// Deliberate lifecycle consequence, not a bug: this object's own
// BoardManager never unbinds. EngineService documents a destroy-on-
// last-unbind design; once either adb-exec or SSH has been used,
// :engine (and any camera session a script opened) now outlives
// MainActivity for as long as this process is alive -- required for
// MicroPython's own interpreter state to persist across separate
// adb/SSH invocations exactly as if typed into the on-screen terminal.
// see session-state: ScriptExecCore.kt#ScriptExecCore
object ScriptExecCore {
    sealed class RunResult {
        data class Ok(val output: String) : RunResult()
        object Busy : RunResult()
        object Disconnected : RunResult()
    }

    private val connectLock = Any()
    // Only "run" is gated -- "reset"/"interrupt" bypass it, matching
    // EngineWorker.interrupt()'s own bypass of its task queue.
    private val busy = AtomicBoolean(false)

    @Volatile private var boardManager: BoardManager? = null
    @Volatile private var connected = false
    @Volatile private var latch: CountDownLatch? = null

    fun run(context: Context, code: String): RunResult {
        if (!busy.compareAndSet(false, true)) {
            return RunResult.Busy
        }
        try {
            ensureConnected(context)
            val output = boardManager?.exec(code) ?: ""
            // "" is ambiguous (no output vs. engine died); check
            // connected rather than trust the string alone -- see
            // BoardManager.exec()'s own RemoteException handling.
            return if (!connected) RunResult.Disconnected else RunResult.Ok(output)
        } finally {
            busy.set(false)
        }
    }

    fun reset(context: Context) {
        ensureConnected(context)
        boardManager?.reset()
    }

    fun interrupt(context: Context) {
        ensureConnected(context)
        boardManager?.interrupt()
    }

    // Scoped to only the connect-and-wait sequence, not around run()
    // itself, so a long-running script doesn't block a concurrent
    // reset/interrupt call on a different caller thread. The status
    // callback sets a @Volatile flag and counts down the latch on every
    // invocation, not just the first, so a later mid-session :engine
    // crash is observed by the next call() rather than silently using a
    // stale reference. Connecting is deliberately excluded from the
    // countDown -- BoardManager.connect() fires it synchronously,
    // before it has even attempted to bind, so counting it down here
    // would release the latch before the real async Connected/
    // Disconnected result ever arrives.
    private fun ensureConnected(context: Context) {
        synchronized(connectLock) {
            if (connected) return

            val bm = boardManager ?: BoardManager(
                context = context.applicationContext,
                registerOutputListener = false,
            ) { status ->
                connected = status is ConnectionStatus.Connected
                if (status !is ConnectionStatus.Connecting) {
                    latch?.countDown()
                }
            }.also { boardManager = it }

            val freshLatch = CountDownLatch(1)
            latch = freshLatch
            bm.connect()
            freshLatch.await(10, TimeUnit.SECONDS)
        }
    }
}
