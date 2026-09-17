package eu.kdvelectronics.upyandroid.managers

/**
 * Thin wrapper over [BoardManager] -- the UI-facing contract, kept
 * separate from BoardManager itself so the transport and the REPL-level
 * operations stay distinct concerns, matching micro-repl's original
 * shape. Much smaller than the original `TerminalManager`: no raw-REPL
 * silent-mode dance (there's no serial link to simulate call/return
 * over), no `executeScript`/`executeLocalScript` (Scripts feature
 * dropped for v1), no `\r`-joining for multi-line code (the AIDL
 * `exec()` takes real source text and MicroPython's compiler handles
 * real newlines fine -- already exercised in native bring-up).
 *
 * All methods block until the engine responds -- call from a background
 * thread/coroutine, never the UI thread.
 */
class TerminalManager(private val boardManager: BoardManager) {
    fun eval(code: String): String = boardManager.exec(code.trim())

    fun terminateExecution() = boardManager.interrupt()

    fun reset() = boardManager.reset()
}
