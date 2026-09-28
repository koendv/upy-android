package eu.kdvelectronics.upyandroid

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

// App-wide (default/UI process), not tied to MainActivity's own
// ViewModel/lifecycle. AdbExecProvider and the SSH shell
// (SshServerManager.kt) both live in this same process and need to
// write into the same terminal transcript MainActivity's own
// TerminalScreen displays. ScriptExecCore's one shared BoardManager
// connection forwards every output chunk here regardless of which
// caller (Terminal, Explorer/Editor Run, adb-exec, SSH) produced it --
// see ScriptExecCore.kt#ScriptExecCore. A plain object plus
// MutableStateFlow, so this keeps working even before MainActivity has
// ever been created (a cold-started adb/SSH-only session).
object TerminalLog {
    // Unbounded `+= chunk` rebuilds the whole string on every chunk, so
    // a long enough session (many runs, or one very long-running fast
    // print loop) makes append() progressively more expensive with no
    // ceiling -- real bug, not yet hit in practice (not the cause of
    // any specific symptom observed so far, see session-state). 8x
    // mphalport.c's own MP_EMBED_OUTPUT_BUF_SIZE (that one bounds a
    // single run's captured output at 32KB; this is cross-run
    // scrollback, so a multiple of it).
    // see session-state: TerminalLog.kt#TerminalLog
    private const val MAX_CHARS = 8 * 32 * 1024

    private val _text = MutableStateFlow("")
    val text = _text.asStateFlow()

    @Synchronized
    fun append(chunk: String) {
        _text.value = (_text.value.takeLast(MAX_CHARS) + chunk).takeLast(MAX_CHARS)
    }

    @Synchronized
    fun clear() {
        _text.value = ""
    }
}
