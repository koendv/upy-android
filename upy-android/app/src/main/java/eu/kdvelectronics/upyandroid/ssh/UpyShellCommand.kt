package eu.kdvelectronics.upyandroid.ssh

import android.content.Context
import eu.kdvelectronics.upyandroid.ScriptExecCore
import eu.kdvelectronics.upyandroid.TerminalLog
import org.apache.sshd.server.Environment
import org.apache.sshd.server.ExitCallback
import org.apache.sshd.server.channel.ChannelSession
import org.apache.sshd.server.command.Command
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

// One SSH shell session. In command mode lines are collected until a blank
// line, then run through ScriptExecCore. While any script waits for input
// (the app's field is in input mode) each line is sent to that script as
// soon as Enter is pressed, also an empty one. Output of a running script
// is shown as it is printed. Ctrl+C interrupts, Ctrl+D resets (there is no
// EOF), exit or quit ends the session.
class UpyShellCommand(private val context: Context) : Command {
    private lateinit var input: InputStream
    private lateinit var output: OutputStream
    private var exitCallback: ExitCallback? = null
    private var thread: Thread? = null

    @Volatile
    private var running = true

    // True while this session's script runs; a second one is refused.
    @Volatile
    private var submitting = false

    // Whether a script, started from anywhere, waits for input.
    @Volatile
    private var inputMode = false

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // Own script: the characters of its output already shown, so that its end
    // shows what has not arrived yet; and how many characters that were, which
    // arrive later as chunks and must not be shown again.
    private val streamLock = Any()
    private var streamed = 0
    private var skip = 0

    override fun setInputStream(input: InputStream) {
        this.input = input
    }

    override fun setOutputStream(output: OutputStream) {
        this.output = output
    }

    override fun setErrorStream(errorStream: OutputStream) {
        // Not used: everything goes to the output stream.
    }

    override fun setExitCallback(callback: ExitCallback) {
        exitCallback = callback
    }

    override fun start(channel: ChannelSession, env: Environment) {
        scope.launch {
            ScriptExecCore.scriptOutput.collect { chunk ->
                synchronized(streamLock) {
                    var text = chunk
                    if (skip > 0) {
                        val n = minOf(skip, text.length)
                        skip -= n
                        text = text.substring(n)
                    }
                    if (text.isNotEmpty()) {
                        streamed += text.length
                        write(text.replace("\n", "\r\n"))
                    }
                }
            }
        }
        scope.launch {
            ScriptExecCore.inputWaiting.collect { waiting ->
                val wasWaiting = inputMode
                inputMode = waiting
                // A script that was started elsewhere ends its input: show the prompt again.
                if (wasWaiting && !waiting && !submitting) write("\r\n>>> ")
            }
        }
        thread = Thread(::runLoop, "upy-ssh-shell").apply {
            isDaemon = true
            start()
        }
    }

    override fun destroy(channel: ChannelSession) {
        running = false
        scope.cancel()
        thread?.interrupt()
    }

    @Synchronized
    private fun writeByte(b: Int) {
        try {
            output.write(b)
            output.flush()
        } catch (e: IOException) {
            // client disconnected
        }
    }

    @Synchronized
    private fun write(s: String) {
        try {
            output.write(s.toByteArray(Charsets.UTF_8))
            output.flush()
        } catch (e: IOException) {
            // client disconnected
        }
    }

    private fun runLoop() {
        write("upy shell\r\nblank line runs the buffer, ctrl+c interrupts, ctrl+d resets, exit/quit closes.\r\n" +
            "While a script asks for input, each line you type is sent to it.\r\n")
        // Joined while a script already waits: the prompt it printed was not seen.
        if (inputMode || ScriptExecCore.inputWaiting.value) {
            inputMode = true
            write("[a script waits for input] " + TerminalLog.lastLineText())
        } else {
            write(">>> ")
        }
        // UTF-8 bytes of the line being typed, decoded on Enter.
        val lineBuf = ByteArrayOutputStream()
        val bufferedLines = StringBuilder()
        try {
            while (running) {
                val b = input.read()
                if (b == -1) break
                when (b) {
                    0x03 -> { // Ctrl+C
                        write("\r\n^C\r\n")
                        ScriptExecCore.interrupt(context)
                        lineBuf.reset()
                        bufferedLines.setLength(0)
                        write(">>> ")
                    }
                    0x04 -> { // Ctrl+D
                        write("\r\n")
                        ScriptExecCore.reset(context)
                        lineBuf.reset()
                        bufferedLines.setLength(0)
                        write("(reset)\r\n>>> ")
                    }
                    '\r'.code, '\n'.code -> {
                        write("\r\n")
                        // Invalid UTF-8 becomes U+FFFD.
                        val line = String(lineBuf.toByteArray(), Charsets.UTF_8)
                        val trimmed = line.trim().lowercase()
                        if (inputMode && ScriptExecCore.sendInput((line + "\n").toByteArray(Charsets.UTF_8)) >= 0) {
                            // Input for the waiting script, not code.
                            lineBuf.reset()
                        } else if (bufferedLines.isEmpty() && (trimmed == "exit" || trimmed == "quit")) {
                            write("bye\r\n")
                            running = false
                        } else if (lineBuf.size() == 0 && bufferedLines.isNotEmpty()) {
                            submitAsync(bufferedLines.toString())
                            bufferedLines.setLength(0)
                        } else {
                            bufferedLines.append(line).append('\n')
                            lineBuf.reset()
                            write("... ")
                        }
                    }
                    0x7f, 0x08 -> { // backspace/DEL
                        // Removes one whole UTF-8 character, erases one column.
                        if (lineBuf.size() > 0) {
                            val bytes = lineBuf.toByteArray()
                            var cut = bytes.size - 1
                            while (cut > 0 && (bytes[cut].toInt() and 0xC0) == 0x80) {
                                cut--
                            }
                            lineBuf.reset()
                            lineBuf.write(bytes, 0, cut)
                            write("\b \b")
                        }
                    }
                    0x1b -> { // ESC: swallow a following simple CSI sequence (arrow keys etc.)
                        val next = input.read()
                        if (next == '['.code) {
                            input.read()
                        }
                    }
                    else -> {
                        // Printable ASCII, tab, and UTF-8 multi-byte characters.
                        if (b == 0x09 || b in 0x20..0x7e || b >= 0x80) {
                            lineBuf.write(b)
                            writeByte(b)
                        }
                    }
                }
            }
        } catch (e: Exception) {
            // Channel closed or IO error. Fall through to exit, same
            // as a real shell's own EOF-on-disconnect handling.
        }
        exitCallback?.onExit(0)
    }

    private fun submitAsync(code: String) {
        if (submitting) {
            write("[a script is already running on this session]\r\n>>> ")
            return
        }
        submitting = true
        synchronized(streamLock) {
            streamed = 0
            skip = 0
        }
        Thread({
            try {
                // Header written by run() itself, once the busy check
                // succeeds -- see ScriptExecCore.kt#ScriptExecCore. Live
                // output already reaches TerminalLog via the shared
                // connection's output listener as the script runs.
                when (val result = ScriptExecCore.run(context, code, label = "ssh")) {
                    // Most of the output has been shown while the script ran; what
                    // has not arrived yet is shown now.
                    is ScriptExecCore.RunResult.Ok -> synchronized(streamLock) {
                        val rest = result.output.drop(streamed)
                        write(rest.replace("\n", "\r\n"))
                        skip = rest.length
                        streamed = 0
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
