package eu.kdvelectronics.upyandroid

import androidx.compose.runtime.mutableStateOf
import eu.kdvelectronics.upyandroid.managers.TerminalHistoryManager
import eu.kdvelectronics.upyandroid.model.ConnectionStatus
import kotlinx.coroutines.flow.MutableStateFlow

/**
 * UI state for the (terminal-only, for v1) main screen. Smaller than
 * micro-repl's original -- no files/scripts state, those features were
 * dropped. Plain class, not an androidx ViewModel -- doesn't need to
 * survive configuration changes for v1, kept simple.
 */
class MainViewModel {
    val status = MutableStateFlow<ConnectionStatus>(ConnectionStatus.Connecting)
    val terminalInput = mutableStateOf("")
    val terminalOutput = mutableStateOf("")
    val history = TerminalHistoryManager()
}
