/*
 * Adapted from Ma7moud3ly/micro-repl (MIT license), unchanged apart from
 * the package name -- self-contained, no dependency on the dropped
 * USB-serial transport.
 */

package eu.kdvelectronics.upyandroid.managers

/**
 * Manages command history for the terminal: push a new command, and
 * navigate it with up()/down(), same as shell history.
 */
class TerminalHistoryManager {
    private var historyIndex = 0
    private val history = mutableListOf<String>()

    fun push(value: String) {
        if (history.contains(value).not()) {
            history.add(value)
            historyIndex = history.size - 1
        }
    }

    fun up(): String? {
        return if (history.isNotEmpty() && historyIndex >= 0) history[historyIndex--] else null
    }

    fun down(): String? {
        if (historyIndex == -1) historyIndex = 0
        if (historyIndex + 1 < history.size) historyIndex++
        return if (history.isNotEmpty()) history[historyIndex] else null
    }
}
