package eu.kdvelectronics.upyandroid.managers

import android.content.Context
import eu.kdvelectronics.upyandroid.ScriptExecCore

/**
 * Thin wrapper over [ScriptExecCore]'s shared connection. The UI-facing
 * contract, kept separate so the transport and the REPL-level
 * operations stay distinct concerns, matching micro-repl's original
 * shape. Much smaller than the original `TerminalManager`: no raw-REPL
 * silent-mode dance (there's no serial link to simulate call/return
 * over), no `executeScript`/`executeLocalScript` (Scripts feature
 * dropped for v1), no `\r`-joining for multi-line code (the AIDL
 * `exec()` takes real source text and MicroPython's compiler handles
 * real newlines fine -- already exercised in native bring-up).
 *
 * eval()/reset() block until the engine responds. Call from a
 * background thread/coroutine, never the UI thread. terminateExecution()
 * is the exception: non-blocking, safe from the UI thread, see its own
 * comment.
 */
class TerminalManager(private val context: Context) {
    // Routed through ScriptExecCore.runQueued() so a script running from
    // here participates in the same shared in-flight signal a concurrent
    // adb-exec/SSH call checks -- see ScriptExecCore.kt#ScriptExecCore.
    fun eval(code: String): String = ScriptExecCore.runQueued(context, code.trim())

    // interruptNow(), not interrupt(context): called directly from a
    // Compose onClick (TerminalScreen's Interrupt button), never wrapped
    // in a coroutine. interrupt(context) blocks via ensureConnected() and
    // would risk a main-thread deadlock here.
    fun terminateExecution() = ScriptExecCore.interruptNow()

    fun reset() = ScriptExecCore.reset(context)
}
