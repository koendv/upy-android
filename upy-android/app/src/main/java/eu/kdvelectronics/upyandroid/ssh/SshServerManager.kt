package eu.kdvelectronics.upyandroid.ssh

import android.content.Context
import android.util.Log
import eu.kdvelectronics.upyandroid.managers.SettingsManager
import org.apache.sshd.common.util.io.PathUtils
import org.apache.sshd.server.SshServer
import org.apache.sshd.server.keyprovider.SimpleGeneratorHostKeyProvider
import java.io.File

// see session-state: SshServerManager.kt#SshServerManager
object SshServerManager {
    private const val TAG = "SshServerManager"
    const val SSH_PORT = 2222
    private const val HOST_KEY_FILE_NAME = ".ssh_host_key"

    @Volatile
    private var server: SshServer? = null

    // see session-state: SshServerManager.kt#SshServerManager
    @Synchronized
    fun applySettings(context: Context, settingsManager: SettingsManager) {
        val shouldRun = settingsManager.sshEnabled
        val running = server != null
        if (shouldRun && !running) {
            start(context, settingsManager)
        } else if (!shouldRun && running) {
            stop()
        }
    }

    private fun start(context: Context, settingsManager: SettingsManager) {
        Log.i(TAG, "starting SSH server on port $SSH_PORT")
        // see session-state: SshServerManager.kt#SshServerManager
        PathUtils.setUserHomeFolderResolver { context.filesDir.toPath() }
        val sshd = SshServer.setUpDefaultServer()
        sshd.port = SSH_PORT
        // see session-state: SshServerManager.kt#SshServerManager
        sshd.keyPairProvider = SimpleGeneratorHostKeyProvider(File(context.filesDir, HOST_KEY_FILE_NAME).toPath())
        sshd.passwordAuthenticator = { _, password, _ ->
            // see session-state: SshServerManager.kt#SshServerManager
            val configured = settingsManager.sshPassword
            configured.isNotEmpty() && password == configured
        }
        sshd.shellFactory = org.apache.sshd.server.shell.ShellFactory { UpyShellCommand(context) }
        sshd.start()
        server = sshd
    }

    private fun stop() {
        Log.i(TAG, "stopping SSH server")
        server?.stop()
        server = null
    }
}
