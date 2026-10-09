/*
 * up()/down() navigation originally adapted from Ma7moud3ly/micro-repl
 * (MIT license); storage (push()) since rewritten, see the class doc
 * comment below. Self-contained, no dependency on the dropped
 * USB-serial transport.
 */

package eu.kdvelectronics.upyandroid.managers

/**
 * Manages command history for the terminal: push a new command, and
 * navigate it with up()/down(), same as shell history.
 *
 * Storage is byte-budget-capped with FIFO eviction, not entry-count
 * capped -- see session-state for the microrl/Arduino IDE reference
 * designs this follows.
 */
class TerminalHistoryManager {
    companion object {
        // Bytes, not entry count: a single entry (a typed
        // or pasted multi-line script) has no inherent size limit, so
        // entry-count capping wouldn't bound worst-case memory the way
        // byte-budget capping does.
        // see session-state: TerminalHistoryManager.kt#TerminalHistoryManager
        private const val MAX_BYTES = 32 * 1024
    }

    private var historyIndex = 0
    private val history = ArrayDeque<String>()
    private var totalBytes = 0

    fun push(value: String) {
        // Dedup against the last entry only, not the whole history --
        // O(1) not O(n), matches microrl's and Arduino IDE's own
        // history implementations.
        if (history.lastOrNull() == value) return
        val valueBytes = value.toByteArray(Charsets.UTF_8).size
        // A single entry bigger than the whole cap is dropped, not
        // kept by evicting everything else -- matches microrl's own
        // verified behavior (prv_hist_save_line's own size check).
        if (valueBytes > MAX_BYTES) return
        while (history.isNotEmpty() && totalBytes + valueBytes > MAX_BYTES) {
            totalBytes -= history.removeFirst().toByteArray(Charsets.UTF_8).size
        }
        history.addLast(value)
        totalBytes += valueBytes
        historyIndex = history.size - 1
    }

    fun up(): String? {
        return if (history.isNotEmpty() && historyIndex >= 0) history[historyIndex--] else null
    }

    fun down(): String? {
        if (history.isEmpty()) return null
        // Reset-from-exhausted and advance-by-one are mutually exclusive,
        // not sequential: the first down() after up() has walked off the
        // oldest entry (historyIndex == -1) must land back on that same
        // oldest entry, matching what the last up() call showed. The
        // original code did both in the same call (reset to 0, then
        // immediately advance to 1), silently skipping index 0 -- real
        // bug, found and fixed 2026-09-28, see session-state.
        if (historyIndex == -1) {
            historyIndex = 0
        } else if (historyIndex + 1 < history.size) {
            historyIndex++
        }
        return history[historyIndex]
    }
}
