package eu.kdvelectronics.upyandroid.ssh

import android.content.Context
import eu.kdvelectronics.upyandroid.ScriptExecCore
import org.apache.sshd.server.Environment
import org.apache.sshd.server.ExitCallback
import org.apache.sshd.server.channel.ChannelSession
import org.apache.sshd.server.command.Command
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

// see session-state: UpyShellCommand.kt#UpyShellCommand
class UpyShellCommand(private val context: Context) : Command {
    private lateinit var input: InputStream
    private lateinit var output: OutputStream
    private var exitCallback: ExitCallback? = null
    private var thread: Thread? = null

    @Volatile
    private var running = true

    // see session-state: UpyShellCommand.kt#UpyShellCommand
    @Volatile
    private var submitting = false

    override fun setInputStream(input: InputStream) {
        this.input = input
    }

    override fun setOutputStream(output: OutputStream) {
        this.output = output
    }

    override fun setErrorStream(errorStream: OutputStream) {
        // see session-state: UpyShellCommand.kt#UpyShellCommand
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

    private fun writeByte(b: Int) {
        try {
            output.write(b)
            output.flush()
        } catch (e: IOException) {
            // client disconnected
        }
    }

    private fun write(s: String) {
        try {
            output.write(s.toByteArray(Charsets.UTF_8))
            output.flush()
        } catch (e: IOException) {
            // client disconnected
        }
    }

    private fun runLoop() {
        write("upy shell\r\nblank line runs the buffer, ctrl+c interrupts, ctrl+d resets, exit/quit closes.\r\n>>> ")
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
                        if (bufferedLines.isEmpty() && (trimmed == "exit" || trimmed == "quit")) {
                            // see session-state: UpyShellCommand.kt#UpyShellCommand
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

    // see session-state: UpyShellCommand.kt#submitAsync
    private fun submitAsync(code: String) {
        // see session-state: UpyShellCommand.kt#submitAsync
        if (submitting) {
            write("[a script is already running on this session]\r\n>>> ")
            return
        }
        submitting = true
        Thread({
            try {
                // Header written by run() itself, once the busy check
                // succeeds -- see ScriptExecCore.kt#ScriptExecCore. Live
                // output already reaches TerminalLog via the shared
                // connection's output listener as the script runs.
                when (val result = ScriptExecCore.run(context, code, label = "ssh")) {
                    is ScriptExecCore.RunResult.Ok -> write(result.output.replace("\n", "\r\n"))
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
