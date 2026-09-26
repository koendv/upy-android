package eu.kdvelectronics.upyandroid.ssh

import android.content.Context
import android.util.Log
import eu.kdvelectronics.upyandroid.managers.SettingsManager
import org.apache.sshd.common.util.io.PathUtils
import org.apache.sshd.server.SshServer
import org.apache.sshd.server.keyprovider.SimpleGeneratorHostKeyProvider
import java.io.File

// Runs in the default/UI process, same as HttpServerManager and
// AdbExecProvider -- Apache MINA SSHD (confirmed choice, see the plan's
// own Part 8 design). Port 8022, not 22: unprivileged Android apps
// cannot bind ports below 1024 (same convention Termux's own sshd
// uses), and this project has no need to imitate a "real" system SSH
// port.
class SshServerManager(private val context: Context, private val settingsManager: SettingsManager) {
    companion object {
        private const val TAG = "SshServerManager"
        const val PORT = 8022
        private const val HOST_KEY_FILE_NAME = ".ssh_host_key"
    }

    @Volatile
    private var server: SshServer? = null

    // Same call-site pattern as HttpServerManager.applySettings() --
    // called once at app start and again after every settings change.
    // ssh_password is read LIVE inside the PasswordAuthenticator lambda
    // below, not captured at server-start time, so changing it while
    // the server is already running takes effect on the very next
    // connection attempt with no restart needed -- only ssh_enabled
    // flipping needs this start/stop check.
    @Synchronized
    fun applySettings() {
        val shouldRun = settingsManager.sshEnabled
        val running = server != null
        if (shouldRun && !running) {
            start()
        } else if (!shouldRun && running) {
            stop()
        }
    }

    private fun start() {
        Log.i(TAG, "starting SSH server on port $PORT")
        // MUST happen before the very first SshServer.setUpDefaultServer()
        // call, and MUST be set again on every start() (a real, on-device
        // crash was hit and fixed here, not a landmine left behind):
        // MINA SSHD's own static init chain (ServerBuilder ->
        // AuthorizedKeysAuthenticator -> PublicKeyEntry ->
        // PathUtils.getUserHomeFolder()) unconditionally resolves a
        // POSIX-style user home directory the first time
        // setUpDefaultServer() runs anywhere in this process, and
        // Android has no such concept -- confirmed via a real crash log:
        // "IllegalArgumentException: No user home folder available...
        // there is no home folder on Android". Idempotent and cheap to
        // call every time, so no separate one-time-init guard is needed.
        PathUtils.setUserHomeFolderResolver { context.filesDir.toPath() }
        val sshd = SshServer.setUpDefaultServer()
        sshd.port = PORT
        // Generated once, persisted under filesDir (the same directory
        // every other per-app private file already lives in) -- a
        // fresh key every restart would make every SSH client complain
        // about a changed host key on every single connection.
        sshd.keyPairProvider = SimpleGeneratorHostKeyProvider(File(context.filesDir, HOST_KEY_FILE_NAME).toPath())
        sshd.passwordAuthenticator = { _, password, _ ->
            // Empty ssh_password means "not configured" -- deny every
            // login rather than treat it as "no auth required". Same
            // fail-closed default as HttpServerManager's own auth gate.
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
