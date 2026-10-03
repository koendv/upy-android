package eu.kdvelectronics.upyandroid.ssh

import android.content.Context
import eu.kdvelectronics.upyandroid.ScriptExecCore
import org.apache.sshd.server.Environment
import org.apache.sshd.server.ExitCallback
import org.apache.sshd.server.channel.ChannelSession
import org.apache.sshd.server.command.Command
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
                        val trimmed = lineBuf.toString().trim().lowercase()
                        if (bufferedLines.isEmpty() && (trimmed == "exit" || trimmed == "quit")) {
                            // see session-state: UpyShellCommand.kt#UpyShellCommand
                            write("bye\r\n")
                            running = false
                        } else if (lineBuf.isEmpty() && bufferedLines.isNotEmpty()) {
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
                    0x1b -> { // ESC: swallow a following simple CSI sequence (arrow keys etc.)
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
