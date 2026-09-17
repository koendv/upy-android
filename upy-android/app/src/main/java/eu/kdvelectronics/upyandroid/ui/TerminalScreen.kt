package eu.kdvelectronics.upyandroid.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.dp
import eu.kdvelectronics.upyandroid.MainViewModel
import eu.kdvelectronics.upyandroid.managers.TerminalManager
import eu.kdvelectronics.upyandroid.model.ConnectionStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TerminalScreen(
    viewModel: MainViewModel,
    terminalManager: TerminalManager,
    status: ConnectionStatus,
    onReconnect: () -> Unit,
    onOpenFiles: () -> Unit,
    onOpenCamera: () -> Unit,
) {
    val coroutineScope = rememberCoroutineScope()
    var input by viewModel.terminalInput
    var output by viewModel.terminalOutput
    val scrollState = rememberScrollState()

    fun run() {
        val code = input
        if (code.isBlank()) return
        viewModel.history.push(code)
        output += "\n>>> $code\n"
        input = ""
        // Output arrives live via the output listener registered once in
        // MainActivity, not from eval()'s return value -- see BoardManager.
        coroutineScope.launch(Dispatchers.IO) {
            terminalManager.eval(code)
        }
    }

    LaunchedEffect(output) {
        scrollState.animateScrollTo(scrollState.maxValue)
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("upy-android") },
                actions = {
                    Text(
                        text = when (status) {
                            is ConnectionStatus.Connecting -> "connecting..."
                            is ConnectionStatus.Connected -> "connected"
                            is ConnectionStatus.Disconnected -> "disconnected"
                        },
                        modifier = Modifier.padding(end = 16.dp)
                    )
                    TextButton(onClick = onOpenFiles) { Text("Files") }
                    TextButton(onClick = onOpenCamera) { Text("Camera") }
                }
            )
        }
    ) { padding: PaddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .imePadding()
                .padding(8.dp)
        ) {
            Text(
                text = output,
                fontFamily = FontFamily.Monospace,
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .verticalScroll(scrollState)
            )

            if (status !is ConnectionStatus.Connected) {
                TextButton(onClick = onReconnect) {
                    Text("Reconnect")
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                TextButton(onClick = {
                    viewModel.history.up()?.let { input = it }
                }) { Text("↑") }
                TextButton(onClick = {
                    viewModel.history.down()?.let { input = it }
                }) { Text("↓") }
                TextButton(onClick = { terminalManager.terminateExecution() }) {
                    Text("Interrupt")
                }
                TextButton(onClick = {
                    coroutineScope.launch(Dispatchers.IO) { terminalManager.reset() }
                    output = ""
                }) { Text("Reset") }
                TextButton(onClick = { output = "" }) { Text("Clear") }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = androidx.compose.ui.Alignment.CenterVertically
            ) {
                OutlinedTextField(
                    value = input,
                    onValueChange = { input = it },
                    modifier = Modifier.weight(1f),
                    textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                    // Code input, not prose: the on-screen keyboard's autocorrect
                    // (on by default) silently mangles valid Python identifiers --
                    // e.g. typing "gc" got autocorrected to "GC" mid-entry, breaking
                    // `import gc` with a NameError, discovered while load-testing
                    // ulab through this exact field. autoCorrectEnabled = false
                    // stops the substitution; capitalization = None (the default,
                    // set explicitly for clarity) stops auto-capitalizing the first
                    // letter of each line.
                    keyboardOptions = KeyboardOptions(
                        imeAction = ImeAction.Send,
                        autoCorrectEnabled = false,
                        capitalization = KeyboardCapitalization.None
                    ),
                    keyboardActions = KeyboardActions(onSend = { run() }),
                    singleLine = false
                )
                Button(onClick = ::run, modifier = Modifier.padding(start = 8.dp)) {
                    Text("Run")
                }
            }
        }
    }
}
