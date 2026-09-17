package eu.kdvelectronics.upyandroid.ui

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue

/** Shared name-prompt dialog (new file/folder, rename, save-as) used by both ExplorerScreen and EditorScreen. */
@Composable
fun NameDialog(
    show: Boolean,
    title: String,
    initial: String = "",
    onDismiss: () -> Unit,
    onOk: (String) -> Unit
) {
    if (!show) return
    var name by remember(show) { mutableStateOf(initial) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                singleLine = true
            )
        },
        confirmButton = {
            TextButton(onClick = { if (name.isNotBlank()) onOk(name) }) { Text("OK") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        }
    )
}
