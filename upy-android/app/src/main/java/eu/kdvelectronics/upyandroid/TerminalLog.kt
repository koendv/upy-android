package eu.kdvelectronics.upyandroid

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

// App-wide (default/UI process), NOT tied to MainActivity's own
// ViewModel/lifecycle -- AdbExecProvider and the SSH shell
// (SshServerManager.kt) both live in this same process and need to
// write into the SAME terminal transcript MainActivity's own
// TerminalScreen displays, regardless of which BoardManager instance
// actually ran the command (MainActivity's own primary one, or
// ScriptExecCore's private registerOutputListener=false one). A plain
// object + MutableStateFlow, so this keeps working even before
// MainActivity has ever been created (a cold-started adb/SSH-only
// session) -- retroactively required for BOTH adb-exec and SSH (see
// the plan's own Part 8 design), previously only an incidental side
// effect of MainActivity happening to be foregrounded/bound.
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
