package eu.kdvelectronics.upyandroid.ssh

import android.content.Context
import eu.kdvelectronics.upyandroid.ScriptExecCore
import eu.kdvelectronics.upyandroid.TerminalLog
import org.apache.sshd.server.Environment
import org.apache.sshd.server.ExitCallback
import org.apache.sshd.server.channel.ChannelSession
import org.apache.sshd.server.command.Command
import java.io.InputStream
import java.io.OutputStream

// One instance per SSH channel (a real interactive shell session, not
// a one-shot command). Confirmed via native-bringup/my-overrides/
// embed_util.c that mp_embed_exec_str already compiles an entire
// submitted string as one unit (MP_PARSE_FILE_INPUT), exactly like the
// on-screen terminal's own multi-line box + Run button -- so this does
// NOT need genuine line-by-line REPL continuation-detection
// (mp_repl_continue_with_input()-style logic). Instead: accumulate a
// multi-line edit buffer, submit the whole thing on a blank line,
// reusing ScriptExecCore.run()/mp_embed_exec_str completely unchanged.
//
// Key bindings, all universal ASCII control codes, no client-side
// config needed: blank line (Enter twice) submits the accumulated
// buffer; Ctrl+C (0x03) interrupts (bypasses the busy gate, matching
// EngineWorker.interrupt()'s own bypass of its task queue); Ctrl+D
// (0x04) resets the shell. All three map 1:1 onto AdbExecProvider's own
// run/interrupt/reset methods, via the SAME shared ScriptExecCore.
//
// No real pty is allocated on this side even when the SSH client
// requests one (pty-req) -- MINA SSHD just tracks the requested
// terminal modes in Environment, it never creates an OS-level pty. A
// real OpenSSH client that believes it has a remote pty puts its own
// local terminal into raw mode (no local echo, every keystroke --
// including backspace/arrows -- sent raw), so this command must do its
// own byte-by-byte echo and backspace handling, or the user sees
// nothing while typing. Arrow keys/other multi-byte ESC sequences are
// swallowed, not interpreted (no line-editing/history over SSH for v1
// -- out of this plan's own stated scope).
class UpyShellCommand(private val context: Context) : Command {
    private lateinit var input: InputStream
    private lateinit var output: OutputStream
    private var exitCallback: ExitCallback? = null
    private var thread: Thread? = null

    @Volatile
    private var running = true

    // Guards against a second blank-line submission overlapping the
    // first (ScriptExecCore itself already gates concurrent runs across
    // ALL callers with its own busy flag, returning Busy -- this is a
    // narrower, same-channel concern: don't even bother calling run()
    // again from this channel while its own previous submission is
    // still in flight). Deliberately NOT checked before Ctrl+C/Ctrl+D --
    // those must always be dispatched immediately, on the read-loop's
    // own thread, regardless of what the submit thread is doing.
    @Volatile
    private var submitting = false

    override fun setInputStream(input: InputStream) {
        this.input = input
    }

    override fun setOutputStream(output: OutputStream) {
        this.output = output
    }

    override fun setErrorStream(errorStream: OutputStream) {
        // Never written to -- this shell has no separate stderr stream
        // of its own, matching the on-screen terminal's own single
        // combined output.
    }

    override fun setExitCallback(callback: ExitCallback) {
        exitCallback = callback
    }

    override fun start(channel: ChannelSession, env: Environment) {
        thread = Thread(::runLoop, "upy-ssh-shell").apply {
            isDaemon = true
            start()
        }
    }

    override fun destroy(channel: ChannelSession) {
        running = false
        thread?.interrupt()
    }

    private fun write(s: String) {
        output.write(s.toByteArray(Charsets.UTF_8))
        output.flush()
    }

    private fun runLoop() {
        write("upy-android SSH shell. Blank line runs the buffer, Ctrl+C interrupts, Ctrl+D resets.\r\n>>> ")
        val lineBuf = StringBuilder()
        val bufferedLines = StringBuilder()
        try {
            while (running) {
                val b = input.read()
                if (b == -1) break
                when (b) {
                    0x03 -> { // Ctrl+C
                        write("\r\n^C\r\n")
                        ScriptExecCore.interrupt(context)
                        lineBuf.setLength(0)
                        bufferedLines.setLength(0)
                        write(">>> ")
                    }
                    0x04 -> { // Ctrl+D
                        write("\r\n")
                        ScriptExecCore.reset(context)
                        lineBuf.setLength(0)
                        bufferedLines.setLength(0)
                        write("(reset)\r\n>>> ")
                    }
                    '\r'.code, '\n'.code -> {
                        write("\r\n")
                        if (lineBuf.isEmpty() && bufferedLines.isNotEmpty()) {
                            submitAsync(bufferedLines.toString())
                            bufferedLines.setLength(0)
                        } else {
                            bufferedLines.append(lineBuf).append('\n')
                            lineBuf.setLength(0)
                            write("... ")
                        }
                    }
                    0x7f, 0x08 -> { // backspace/DEL
                        if (lineBuf.isNotEmpty()) {
                            lineBuf.deleteCharAt(lineBuf.length - 1)
                            write("\b \b")
                        }
                    }
                    0x1b -> { // ESC -- swallow a following simple CSI sequence (arrow keys etc.)
                        val next = input.read()
                        if (next == '['.code) {
                            input.read()
                        }
                    }
                    else -> {
                        if (b in 0x20..0x7e) {
                            val ch = b.toChar()
                            lineBuf.append(ch)
                            write(ch.toString())
                        }
                    }
                }
            }
        } catch (e: Exception) {
            // Channel closed / IO error -- fall through to exit, same
            // as a real shell's own EOF-on-disconnect handling.
        }
        exitCallback?.onExit(0)
    }

    // Runs ScriptExecCore.run() on its own thread, never the read-loop's
    // own -- a script that runs for a while (or forever, until Ctrl+C)
    // must not block this channel from reading the very next byte, or
    // Ctrl+C could never be delivered while it's running. See this
    // class's own header comment and the `submitting` field's comment
    // above for why.
    private fun submitAsync(code: String) {
        // Only ever set true here, on the read loop's own single thread
        // (the submit thread only ever sets it back to false, in its
        // own finally block below) -- no genuine race, a plain
        // @Volatile boolean is enough, no AtomicBoolean/CAS needed.
        if (submitting) {
            write("[a script is already running on this session]\r\n>>> ")
            return
        }
        submitting = true
        Thread({
            try {
                when (val result = ScriptExecCore.run(context, code)) {
                    is ScriptExecCore.RunResult.Ok -> {
                        // Retroactive requirement (Part 8): SSH's
                        // commands and output must show up in the
                        // on-screen terminal too -- see TerminalLog.kt's
                        // own header comment.
                        TerminalLog.append("\n>>> (ssh)\n$code\n${result.output}\n")
                        write(result.output.replace("\n", "\r\n"))
                    }
                    ScriptExecCore.RunResult.Busy -> write("[busy]")
                    ScriptExecCore.RunResult.Disconnected -> write("[disconnected]")
                }
            } finally {
                submitting = false
                write("\r\n>>> ")
            }
        }, "upy-ssh-submit").apply {
            isDaemon = true
            start()
        }
    }
}
