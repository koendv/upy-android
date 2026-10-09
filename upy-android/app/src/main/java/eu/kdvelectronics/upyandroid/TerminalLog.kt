package eu.kdvelectronics.upyandroid

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

// One row of the terminal transcript. text never includes a trailing
// '\n' -- TerminalScreen renders one Line per fixed-height LazyColumn
// row, and an embedded '\n' inside a Text's own string still forces a
// layout line break regardless of softWrap, which would break that
// fixed-height assumption. complete tracks whether a '\n' has actually
// been seen yet for this line (false = still a pending partial line,
// the next append() call may extend it further).
data class Line(val text: String, val complete: Boolean, val bytes: Int)

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
//
// A Line list, not a single String: TerminalScreen renders this as a
// LazyColumn (virtualized -- only visible rows laid out), which needs
// discrete rows, not one giant reflowed string. Modeled on Arduino
// IDE 2's own Serial Monitor (a react-window FixedSizeList solving the
// identical growing/auto-scrolling device-log problem) and microrl's
// real ring-buffer history algorithm for the eviction pattern -- see
// session-state.
object TerminalLog {
    // Cross-run scrollback.
    // see session-state: TerminalLog.kt#TerminalLog
    private const val MAX_BYTES = 8 * 32 * 1024

    private val buffer = ArrayDeque<Line>()
    private var totalBytes = 0

    private val _lines = MutableStateFlow<List<Line>>(emptyList())
    val lines = _lines.asStateFlow()

    @Synchronized
    fun append(chunk: String) {
        // Unlike Arduino IDE's own messagesToLines() (which only checks
        // whether the PREVIOUS line ended in '\n', never scans the
        // incoming chunk itself for embedded newlines -- harmless for
        // them since serial data arrives in small fragments), a single
        // chunk here can contain a full multi-line traceback, so this
        // must split on every '\n' the chunk itself contains.
        var start = 0
        while (start < chunk.length) {
            val newlineIndex = chunk.indexOf('\n', start)
            val end = if (newlineIndex == -1) chunk.length else newlineIndex + 1
            appendPiece(chunk.substring(start, end))
            start = end
        }
        enforceCap()
        publish()
    }

    private fun appendPiece(rawPiece: String) {
        val endsLine = rawPiece.endsWith('\n')
        val piece = if (endsLine) rawPiece.dropLast(1) else rawPiece
        val pieceBytes = piece.toByteArray(Charsets.UTF_8).size
        val last = buffer.lastOrNull()
        if (last == null || last.complete) {
            buffer.addLast(Line(piece, endsLine, pieceBytes))
        } else {
            buffer.removeLast()
            buffer.addLast(Line(last.text + piece, endsLine, last.bytes + pieceBytes))
        }
        totalBytes += pieceBytes
    }

    // Evicts oldest whole lines first, same FIFO front-eviction pattern
    // as TerminalHistoryManager's own byte cap. buffer.size > 1 guards
    // against evicting a single pathological still-growing line (e.g.
    // print(huge_string, end='') never produces a '\n') out from under
    // itself -- once it's the only line left, let it keep growing
    // rather than corrupt/lose the in-progress line.
    private fun enforceCap() {
        while (buffer.size > 1 && totalBytes > MAX_BYTES) {
            totalBytes -= buffer.removeFirst().bytes
        }
    }

    private fun publish() {
        // A reference-copy of a short list (at most a few hundred Line
        // objects even at the full cap), not the O(current-total-size)
        // content copy the old String-based takeLast() approach did --
        // cheap enough per chunk, no explicit throttling needed.
        _lines.value = buffer.toList()
    }

    @Synchronized
    fun clear() {
        buffer.clear()
        totalBytes = 0
        publish()
    }
}
