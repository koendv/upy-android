package eu.kdvelectronics.upyandroid

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

// App-wide (default/UI process), NOT tied to MainActivity's own
// ViewModel/lifecycle -- AdbExecProvider and the SSH shell
// (SshServerManager.kt) both live in this same process and need to
// write into the same terminal transcript MainActivity's own
// TerminalScreen displays. ScriptExecCore's one shared BoardManager
// connection forwards every output chunk here regardless of which
// caller (Terminal, Explorer/Editor Run, adb-exec, SSH) produced it --
// see ScriptExecCore.kt#ScriptExecCore. A plain object + MutableStateFlow,
// so this keeps working even before MainActivity has ever been created
// (a cold-started adb/SSH-only session).
object TerminalLog {
    private val _text = MutableStateFlow("")
    val text = _text.asStateFlow()

    @Synchronized
    fun append(chunk: String) {
        _text.value += chunk
    }

    @Synchronized
    fun clear() {
        _text.value = ""
    }
}
