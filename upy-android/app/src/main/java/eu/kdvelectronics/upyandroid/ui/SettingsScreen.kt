package eu.kdvelectronics.upyandroid.ui

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.os.Process
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import eu.kdvelectronics.upyandroid.MainActivity
import eu.kdvelectronics.upyandroid.managers.SettingsManager

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
// inline dialog -- six-plus settings is too much for the dialog Part 1
// used for adb-exec alone. onSettingsChanged fires after every edit so
// the caller can push the new snapshot into a live :engine connection
// immediately (see BoardManager.kt#pushSettings); heap_size_mb still
// only takes effect at the next Reset regardless.
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    settingsManager: SettingsManager,
    onSettingsChanged: () -> Unit,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    // Captured once, at screen open -- the heap size the CURRENTLY
    // running :engine process actually started with. Compared against
    // on Back to decide whether a restart is even relevant.
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

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                navigationIcon = {
                    TextButton(onClick = {
                        if (heapSizeMb.toIntOrNull() != initialHeapSizeMb) {
                            showRestartDialog = true
                        } else {
                            onBack()
                        }
                    }) { Text("Back") }
                }
            )
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
                    // Not pushed via onSettingsChanged() -- heap size is
                    // read once, at process start, only. See
                    // Engine.kt#nativeSetSettings.
                    text.toIntOrNull()?.let { settingsManager.heapSizeMb = it }
                },
                label = { Text("Heap size (MB) -- takes effect after the app is restarted") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.fillMaxWidth()
            )

            SettingsRow("Enable SSH", sshEnabled) {
                sshEnabled = it
                settingsManager.sshEnabled = it
                onSettingsChanged()
            }
            OutlinedTextField(
                value = sshPassword,
                onValueChange = {
                    sshPassword = it
                    settingsManager.sshPassword = it
                    // Not pushed via onSettingsChanged -- passwords
                    // never cross into :engine. See SettingsManager.kt.
                },
                label = { Text("SSH password") },
                visualTransformation = PasswordVisualTransformation(),
                modifier = Modifier.fillMaxWidth()
            )

            SettingsRow("Enable HTTP server", httpServerEnabled) {
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
                visualTransformation = PasswordVisualTransformation(),
                modifier = Modifier.fillMaxWidth()
            )
            SettingsRow(
                "Serve private files over HTTP",
                httpPrivateFilesEnabled,
                enabled = httpServerEnabled
            ) {
                httpPrivateFilesEnabled = it
                settingsManager.httpPrivateFilesEnabled = it
                onSettingsChanged()
            }

            SettingsRow("Enable LiteRT Play Store access", litertPlaystoreEnabled) {
                litertPlaystoreEnabled = it
                settingsManager.litertPlaystoreEnabled = it
                onSettingsChanged()
            }

            SettingsRow("Enable adb exec", adbExecEnabled) {
                adbExecEnabled = it
                settingsManager.adbExecEnabled = it
                onSettingsChanged()
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
                TextButton(onClick = {
                    showRestartDialog = false
                    onBack()
                }) { Text("Cancel") }
            }
        )
    }
}

@Composable
private fun SettingsRow(
    label: String,
    checked: Boolean,
    enabled: Boolean = true,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onCheckedChange, enabled = enabled)
    }
}
