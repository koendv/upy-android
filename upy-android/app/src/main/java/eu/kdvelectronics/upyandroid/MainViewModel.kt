package eu.kdvelectronics.upyandroid

import androidx.compose.runtime.mutableStateOf
import eu.kdvelectronics.upyandroid.managers.TerminalHistoryManager

/**
 * UI state for the (terminal-only, for v1) main screen. Smaller than
 * micro-repl's original: no files/scripts state, those features were
 * dropped. Plain class, not an androidx ViewModel. Doesn't need to
 * survive configuration changes for v1, kept simple.
 *
 * terminalOutput deliberately does not live here any more. See
 * TerminalLog.kt's own header comment for why the transcript itself
 * had to become a process-wide singleton, not per-ViewModel state,
 * once adb-exec/SSH needed to write into the same terminal a script
 * typed directly into the UI produces.
 *
 * Connection status lives in ScriptExecCore.status, not here either --
 * one process-wide connection now (see ScriptExecCore.kt#ScriptExecCore),
 * so a per-ViewModel copy would just duplicate it.
 */
class MainViewModel {
    val terminalInput = mutableStateOf("")
    val history = TerminalHistoryManager()
}
