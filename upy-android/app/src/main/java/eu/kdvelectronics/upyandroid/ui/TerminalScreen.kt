package eu.kdvelectronics.upyandroid.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.ScrollState
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
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import eu.kdvelectronics.upyandroid.MainViewModel
import eu.kdvelectronics.upyandroid.TerminalLog
import eu.kdvelectronics.upyandroid.managers.TerminalManager
import eu.kdvelectronics.upyandroid.model.ConnectionStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

// Compose Foundation has no built-in scrollbar for a plain ScrollState
// on Android (unlike classic View, unlike JetBrains Compose
// Multiplatform's Desktop-only VerticalScrollbar) -- confirmed by
// inspecting the actual foundation jars for this project's BOM. Drawn
// by hand instead.
private fun Modifier.simpleVerticalScrollbar(
    state: ScrollState,
    width: Dp = 4.dp,
    minThumbHeight: Dp = 24.dp,
): Modifier = composed {
    val alpha by animateFloatAsState(
        targetValue = if (state.isScrollInProgress) 1f else 0f,
        animationSpec = tween(durationMillis = if (state.isScrollInProgress) 150 else 500)
    )
    drawWithContent {
        drawContent()
        if (state.maxValue > 0) {
            val viewportHeight = size.height
            val contentHeight = viewportHeight + state.maxValue
            // A long-running fps loop can push contentHeight far past
            // viewportHeight, shrinking the proportional thumb to a
            // near-invisible sliver -- confirmed on-device. Clamped to
            // stay visible/grabbable regardless of log length.
            val thumbHeight = (viewportHeight * (viewportHeight / contentHeight))
                .coerceAtLeast(minThumbHeight.toPx())
                .coerceAtMost(viewportHeight)
            val thumbOffsetY = (viewportHeight - thumbHeight) * (state.value.toFloat() / state.maxValue)
            drawRoundRect(
                color = Color.Gray,
                topLeft = Offset(size.width - width.toPx(), thumbOffsetY),
                size = Size(width.toPx(), thumbHeight),
                alpha = alpha,
                cornerRadius = CornerRadius(width.toPx() / 2, width.toPx() / 2)
            )
        }
    }
}

// Files/Camera/Settings navigation moved to the top-level nav suite
// (MainActivity's own NavigationSuiteScaffold). This screen only owns
// its own terminal chrome now.
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TerminalScreen(
    viewModel: MainViewModel,
    terminalManager: TerminalManager,
    status: ConnectionStatus,
    onReconnect: () -> Unit,
) {
    val coroutineScope = rememberCoroutineScope()
    var input by viewModel.terminalInput
    // Process-wide, not per-ViewModel. See TerminalLog.kt's own
    // header comment (adb-exec/SSH both write into this same log).
    val output by TerminalLog.text.collectAsState()
    val scrollState = rememberScrollState()

    fun run() {
        val code = input
        if (code.isBlank()) return
        viewModel.history.push(code)
        TerminalLog.append("\n>>> $code\n")
        input = ""
        // Output arrives live via the output listener registered once in
        // MainActivity, not from eval()'s return value. See BoardManager.kt.
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
                title = { Text("Command") },
                actions = {
                    // Connecting reuses link_off (user's own call). The
                    // sheet has no dedicated "connecting" glyph, and
                    // link_off's "not currently connected" read fits a
                    // transient state well enough.
                    Symbol(
                        codepoint = if (status is ConnectionStatus.Connected) SymbolIcon.LINK else SymbolIcon.LINK_OFF,
                        contentDescription = when (status) {
                            is ConnectionStatus.Connecting -> "connecting..."
                            is ConnectionStatus.Connected -> "connected"
                            is ConnectionStatus.Disconnected -> "disconnected"
                        },
                        modifier = Modifier.padding(end = 16.dp)
                    )
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
                    .simpleVerticalScrollbar(scrollState)
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
                TooltipIconButton(
                    icon = SymbolIcon.KEYBOARD_ARROW_UP,
                    label = "Previous line",
                    onClick = { viewModel.history.up()?.let { input = it } },
                )
                TooltipIconButton(
                    icon = SymbolIcon.KEYBOARD_ARROW_DOWN,
                    label = "Next line",
                    onClick = { viewModel.history.down()?.let { input = it } },
                )
                TooltipIconButton(
                    icon = SymbolIcon.STOP,
                    label = "Interrupt",
                    onClick = { terminalManager.terminateExecution() },
                )
                TooltipIconButton(
                    icon = SymbolIcon.RESTART_ALT,
                    label = "Reset",
                    onClick = {
                        coroutineScope.launch(Dispatchers.IO) { terminalManager.reset() }
                        TerminalLog.clear()
                    },
                )
                TooltipIconButton(
                    icon = SymbolIcon.DELETE_SWEEP,
                    label = "Clear",
                    onClick = { TerminalLog.clear() },
                )
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
                    // Code input, not prose. The on-screen keyboard's autocorrect
                    // (on by default) silently mangles valid Python identifiers:
                    // typing "gc" can autocorrect to "GC" mid-entry, breaking
                    // `import gc` with a NameError. autoCorrectEnabled = false
                    // stops the substitution. capitalization = None (the default,
                    // set explicitly for clarity) stops auto-capitalizing the
                    // first letter of each line.
                    keyboardOptions = KeyboardOptions(
                        imeAction = ImeAction.Send,
                        autoCorrectEnabled = false,
                        capitalization = KeyboardCapitalization.None
                    ),
                    keyboardActions = KeyboardActions(onSend = { run() }),
                    singleLine = false
                )
                Button(onClick = ::run, modifier = Modifier.padding(start = 8.dp)) {
                    Symbol(SymbolIcon.PLAY_ARROW, contentDescription = "Run")
                }
            }
        }
    }
}
