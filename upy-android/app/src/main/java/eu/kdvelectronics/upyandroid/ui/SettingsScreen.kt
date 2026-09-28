package eu.kdvelectronics.upyandroid.ui

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.os.Process
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import eu.kdvelectronics.upyandroid.MainActivity
import eu.kdvelectronics.upyandroid.managers.SettingsManager

private val ICON_SIZE = 20.dp

// see session-state: SettingsScreen.kt#restartApp
private fun restartApp(context: Context) {
    val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
    val engineProcessName = "${context.packageName}:engine"
    am.runningAppProcesses?.firstOrNull { it.processName == engineProcessName }?.let {
        Process.killProcess(it.pid)
    }
    val intent = Intent(context, MainActivity::class.java).apply {
        addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
    }
    context.startActivity(intent)
    Runtime.getRuntime().exit(0)
}

// Peer top-level destination (matching Command/Files/Camera), not an
// inline dialog: six-plus settings is too much for the dialog Part 1
// used for adb-exec alone. onSettingsChanged fires after every edit so
// the caller can push the new snapshot into a live :engine connection
// immediately (see BoardManager.kt#pushSettings). heap_size_mb still
// only takes effect at the next Reset regardless.
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    settingsManager: SettingsManager,
    onSettingsChanged: () -> Unit,
    onOpenAbout: () -> Unit,
) {
    val context = LocalContext.current
    // Captured once, at screen open: the heap size the currently
    // running :engine process actually started with. Compared against
    // the live field below to decide whether a restart is relevant.
    val initialHeapSizeMb = remember { settingsManager.heapSizeMb }
    var heapSizeMb by remember { mutableStateOf(settingsManager.heapSizeMb.toString()) }
    var showRestartDialog by remember { mutableStateOf(false) }
    var sshEnabled by remember { mutableStateOf(settingsManager.sshEnabled) }
    var sshPassword by remember { mutableStateOf(settingsManager.sshPassword) }
    var httpServerEnabled by remember { mutableStateOf(settingsManager.httpServerEnabled) }
    var httpPassword by remember { mutableStateOf(settingsManager.httpPassword) }
    var httpPrivateFilesEnabled by remember { mutableStateOf(settingsManager.httpPrivateFilesEnabled) }
    var litertPlaystoreEnabled by remember { mutableStateOf(settingsManager.litertPlaystoreEnabled) }
    var adbExecEnabled by remember { mutableStateOf(settingsManager.adbExecEnabled) }
    // Settings is a peer nav-suite tab, not a screen with its own Back
    // button. The old "only prompt on Back" trigger is gone, since a
    // tab switch/system-back gesture no longer routes through any
    // handler this screen owns. Re-homed as an inline row, always
    // visible whenever the field differs from the running process's
    // own heap size, regardless of how (or whether) the user then
    // leaves this screen. Can't be silently skipped by any particular
    // exit path, since it isn't tied to one.
    val heapSizeChanged = heapSizeMb.toIntOrNull() != initialHeapSizeMb

    Scaffold(
        topBar = {
            TopAppBar(title = { Text("Settings") })
        }
    ) { padding: PaddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp)
                .verticalScroll(rememberScrollState())
        ) {
            OutlinedTextField(
                value = heapSizeMb,
                onValueChange = { text ->
                    heapSizeMb = text
                    // Not pushed via onSettingsChanged(). Heap size is
                    // read once, at process start, only. See
                    // Engine.kt#nativeSetSettings.
                    text.toIntOrNull()?.let { settingsManager.heapSizeMb = it }
                },
                label = { Text("Heap size (MB) -- takes effect after the app is restarted") },
                leadingIcon = { Symbol(SymbolIcon.MEMORY, contentDescription = null, size = ICON_SIZE) },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier
                    .fillMaxWidth()
                    // Restores the old Back-button trigger's actual effect
                    // (an automatic confirm prompt), not just the always-
                    // visible reminder row below. Losing focus is this
                    // screen's own equivalent of "the user is done editing
                    // this field", now that there's no Back button to hook.
                    .onFocusChanged { focusState ->
                        if (!focusState.isFocused && heapSizeChanged) {
                            showRestartDialog = true
                        }
                    }
            )
            if (heapSizeChanged) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Restart to apply the new heap size", modifier = Modifier.weight(1f))
                    TextButton(onClick = { showRestartDialog = true }) { Text("Restart") }
                }
            }

            SettingsRow("Enable SSH", sshEnabled, icon = SymbolIcon.TERMINAL_2) {
                sshEnabled = it
                settingsManager.sshEnabled = it
                onSettingsChanged()
            }
            OutlinedTextField(
                value = sshPassword,
                onValueChange = {
                    sshPassword = it
                    settingsManager.sshPassword = it
                    // Not pushed via onSettingsChanged. Passwords
                    // never cross into :engine. See SettingsManager.kt.
                },
                label = { Text("SSH password") },
                leadingIcon = { Symbol(SymbolIcon.PASSWORD_2, contentDescription = null, size = ICON_SIZE) },
                visualTransformation = PasswordVisualTransformation(),
                modifier = Modifier.fillMaxWidth()
            )

            SettingsRow("Enable HTTP server", httpServerEnabled, icon = SymbolIcon.PUBLIC) {
                httpServerEnabled = it
                settingsManager.httpServerEnabled = it
                if (!it) {
                    httpPrivateFilesEnabled = false
                    settingsManager.httpPrivateFilesEnabled = false
                }
                onSettingsChanged()
            }
            OutlinedTextField(
                value = httpPassword,
                onValueChange = {
                    httpPassword = it
                    settingsManager.httpPassword = it
                },
                label = { Text("HTTP password") },
                leadingIcon = { Symbol(SymbolIcon.PASSWORD_2, contentDescription = null, size = ICON_SIZE) },
                visualTransformation = PasswordVisualTransformation(),
                modifier = Modifier.fillMaxWidth()
            )
            SettingsRow(
                "Serve private files over HTTP",
                httpPrivateFilesEnabled,
                icon = SymbolIcon.LOCK,
                enabled = httpServerEnabled
            ) {
                httpPrivateFilesEnabled = it
                settingsManager.httpPrivateFilesEnabled = it
                onSettingsChanged()
            }

            SettingsRow("Enable LiteRT Play Store access", litertPlaystoreEnabled, icon = SymbolIcon.SHOP) {
                litertPlaystoreEnabled = it
                settingsManager.litertPlaystoreEnabled = it
                onSettingsChanged()
            }

            SettingsRow("Enable adb exec", adbExecEnabled, icon = SymbolIcon.ADB) {
                adbExecEnabled = it
                settingsManager.adbExecEnabled = it
                onSettingsChanged()
            }

            // Same "About phone"-style row real Android Settings puts at
            // the bottom of its own list. Navigates to a non-peer
            // detail screen (AboutScreen), same tier as EditorScreen.
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable(onClick = onOpenAbout)
                    .padding(vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("About", modifier = Modifier.weight(1f))
                Text(">")
            }
        }
    }

    if (showRestartDialog) {
        AlertDialog(
            onDismissRequest = { showRestartDialog = false },
            title = { Text("Restart with new heap size?") },
            confirmButton = {
                TextButton(onClick = { restartApp(context) }) { Text("OK") }
            },
            dismissButton = {
                TextButton(onClick = { showRestartDialog = false }) { Text("Cancel") }
            }
        )
    }
}

@Composable
private fun SettingsRow(
    label: String,
    checked: Boolean,
    icon: Int,
    enabled: Boolean = true,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Symbol(icon, contentDescription = null, modifier = Modifier.padding(end = 12.dp), size = ICON_SIZE)
        Text(label, modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onCheckedChange, enabled = enabled)
    }
}
