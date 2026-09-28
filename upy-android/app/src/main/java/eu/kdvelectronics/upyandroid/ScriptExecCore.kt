package eu.kdvelectronics.upyandroid

import android.content.Context
import android.view.Surface
import eu.kdvelectronics.upyandroid.managers.BoardManager
import eu.kdvelectronics.upyandroid.model.ConnectionStatus
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

// The one shared BoardManager connection for the whole process: Terminal
// (MainActivity), Explorer/Editor Run, AdbExecProvider, and the SSH
// shell (SshServerManager.kt) all delegate here. Owns the connection,
// registers the output listener (forwarding every chunk into
// TerminalLog) and the share-request-listener passthrough once,
// permanently, so live output and correct busy reporting work the same
// way for every caller instead of only two of four.
// see session-state: ScriptExecCore.kt#ScriptExecCore
object ScriptExecCore {
    sealed class RunResult {
        data class Ok(val output: String) : RunResult()
        object Busy : RunResult()
        object Disconnected : RunResult()
    }

    private val connectLock = Any()
    // Shared in-flight counter, not a plain boolean: runQueued() below
    // (the UI path -- Terminal input, Explorer/Editor Run) never
    // rejects, so more than one caller can be genuinely in flight at
    // once. A boolean toggled true-on-start/false-on-done would let one
    // caller's completion clear a flag a still-running second caller
    // needs, making a concurrent run() see "not busy" incorrectly.
    // compareAndSet(0, 1) below only succeeds when nothing at all is
    // in flight, from any caller.
    // see session-state: ScriptExecCore.kt#ScriptExecCore
    private val inFlight = AtomicInteger(0)

    private val _status = MutableStateFlow<ConnectionStatus>(ConnectionStatus.Connecting)
    val status: StateFlow<ConnectionStatus> = _status.asStateFlow()

    @Volatile private var boardManager: BoardManager? = null
    @Volatile private var connected = false
    @Volatile private var latch: CountDownLatch? = null

    // Non-blocking: ensures the shared connection exists and starts
    // connecting, without waiting for the result. Safe to call from the
    // main thread (MainActivity.onCreate(), the Terminal's Reconnect
    // button). Never call ensureConnected() from the main thread
    // instead -- see that function's own comment.
    fun connect(context: Context) {
        synchronized(connectLock) {
            ensureBoardManager(context).connect()
        }
    }

    // Binder-facing (adb-exec, SSH): reject-on-busy, so a stuck script
    // can't block a Binder thread-pool thread indefinitely. label, when
    // given, writes a small header into TerminalLog once the busy slot
    // is acquired (e.g. "adb-exec"/"ssh") -- done here, not by the
    // caller before calling run(), so a Busy rejection never leaves an
    // orphaned header with no output behind it.
    fun run(context: Context, code: String, label: String? = null): RunResult {
        if (!inFlight.compareAndSet(0, 1)) {
            return RunResult.Busy
        }
        try {
            ensureConnected(context)
            if (label != null) {
                TerminalLog.append("\n>>> ($label)\n$code\n")
            }
            val output = boardManager?.exec(code) ?: ""
            return if (!connected) RunResult.Disconnected else RunResult.Ok(output)
        } finally {
            inFlight.decrementAndGet()
        }
    }

    // UI-driven Run (Terminal input, Explorer/Editor Run): queue-and-
    // wait, never Busy -- near-simultaneous UI taps should still both
    // eventually run. Call from a background thread/coroutine, never
    // the UI thread (ensureConnected() below can block). No header
    // written here: Terminal/Explorer/Editor already write their own
    // before calling this, see TerminalScreen.kt's run().
    fun runQueued(context: Context, code: String): String {
        inFlight.incrementAndGet()
        try {
            ensureConnected(context)
            return boardManager?.exec(code) ?: ""
        } finally {
            inFlight.decrementAndGet()
        }
    }

    fun reset(context: Context) {
        ensureConnected(context)
        boardManager?.reset()
    }

    // Binder-facing interrupt (adb-exec, SSH's Ctrl+C) -- blocks via
    // ensureConnected() like run()/reset(). Never call from the main
    // thread; see interruptNow() for that.
    fun interrupt(context: Context) {
        ensureConnected(context)
        boardManager?.interrupt()
    }

    // Plain, non-blocking passthrough for main-thread callers (the
    // Terminal's own Interrupt button, CameraScreen's Interrupt
    // button -- ordinary Compose onClick, never wrapped in a
    // coroutine). No ensureConnected() call, so no deadlock risk.
    // boardManager == null (nothing has connected yet at all) or its
    // own engine == null (connecting, not yet bound) is a silent no-op,
    // matching interrupt()'s own behavior before a connection exists.
    fun interruptNow() {
        boardManager?.interrupt()
    }

    fun pushSettings() {
        boardManager?.pushSettings()
    }

    fun setDisplaySurface(surface: Surface?) {
        boardManager?.setDisplaySurface(surface)
    }

    fun setShareRequestListener(listener: ((path: String, mimeType: String) -> Unit)?) {
        boardManager?.setShareRequestListener(listener)
    }

    private fun ensureBoardManager(context: Context): BoardManager {
        return boardManager ?: BoardManager(
            context = context.applicationContext,
        ) { status ->
            connected = status is ConnectionStatus.Connected
            _status.value = status
            if (status !is ConnectionStatus.Connecting) {
                latch?.countDown()
            }
        }.also {
            it.setOutputListener { chunk -> TerminalLog.append(chunk) }
            boardManager = it
        }
    }

    // Blocks the calling thread on a connect result (up to 10s). Must
    // never run on the main thread: BoardManager's ServiceConnection
    // callbacks are delivered on the main looper, so a main-thread
    // caller blocking on the same latch those callbacks count down
    // would deadlock. run()/runQueued()/reset()/interrupt() all call
    // this from a Binder thread-pool thread or an IO-dispatcher
    // coroutine, never directly from a Compose onClick.
    private fun ensureConnected(context: Context) {
        synchronized(connectLock) {
            val bm = ensureBoardManager(context)
            if (connected) return

            val freshLatch = CountDownLatch(1)
            latch = freshLatch
            bm.connect()
            freshLatch.await(10, TimeUnit.SECONDS)
        }
    }
}
