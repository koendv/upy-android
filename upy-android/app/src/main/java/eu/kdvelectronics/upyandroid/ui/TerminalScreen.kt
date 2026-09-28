package eu.kdvelectronics.upyandroid.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LocalContentColor
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.dp
import eu.kdvelectronics.upyandroid.MainViewModel
import eu.kdvelectronics.upyandroid.TerminalLog
import eu.kdvelectronics.upyandroid.managers.TerminalManager
import eu.kdvelectronics.upyandroid.model.ConnectionStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import my.nanihadesuka.compose.LazyColumnScrollbar
import my.nanihadesuka.compose.ScrollbarSettings

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
    val lines by TerminalLog.lines.collectAsState()
    val listState = rememberLazyListState()
    // Shared across every row so they all scroll horizontally in sync,
    // not independently -- rows never wrap (see the Text below), so a
    // long line scrolls sideways like a real terminal, matching Arduino
    // IDE's own Serial Monitor (itemSize-fixed + whiteSpace: nowrap).
    val horizontalScrollState = rememberScrollState()
    // Explicit toggle, default on, not implicit at-bottom detection --
    // matches Arduino IDE's own monitorModel.autoscroll. see session-state.
    var autoscroll by remember { mutableStateOf(true) }

    fun run() {
        val code = input
        if (code.isBlank() || status !is ConnectionStatus.Connected) return
        viewModel.history.push(code)
        TerminalLog.append("\n>>> $code\n")
        input = ""
        // Output arrives live via the output listener registered once in
        // MainActivity, not from eval()'s return value. See BoardManager.kt.
        coroutineScope.launch(Dispatchers.IO) {
            terminalManager.eval(code)
        }
    }

    // Trailing-edge throttle, not a continuous poll: the first
    // lines.size change starts a 100ms timer; further changes arriving
    // while that timer is running are discarded (no-op, not queued);
    // when the timer ends, scroll once using whatever lines.size is
    // AT THAT MOMENT (not the value that started the timer). Caps
    // scrollToItem() at 10/sec under a fast print loop (was
    // 20-25/sec, one per lines.size change) while doing zero work when
    // idle, unlike a fixed-interval poll. see session-state.
    LaunchedEffect(autoscroll) {
        if (!autoscroll) return@LaunchedEffect
        var timerActive = false
        snapshotFlow { lines.size }.collect {
            if (!timerActive) {
                timerActive = true
                launch {
                    // 100ms, not a rounder-looking number picked blind:
                    // measured via dumpsys gfxinfo (lcd_shield.py, ~60s,
                    // same device) at 0/100/200ms. Frame-time percentiles
                    // (the perceptually-relevant metric) were already
                    // flat between 100 and 200ms (50th 20ms->19ms, 90th
                    // 53ms->48ms) -- imperceptible to a human either way.
                    // Janky-frame % kept dropping at 200ms (41%->32%),
                    // but that metric counts ANY missed deadline
                    // regardless of margin, so a slower cadence
                    // mechanically improves it just by doing less work,
                    // without the difference being something a user
                    // would feel. 100ms keeps autoscroll visually live
                    // (10 updates/sec) without paying for a frame-time
                    // improvement that doesn't exist. see session-state.
                    delay(100)
                    if (autoscroll && lines.isNotEmpty()) listState.scrollToItem(lines.size - 1)
                    timerActive = false
                }
            }
        }
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
            // LazyColumnScrollbar's own gesture handling swallows drags
            // meant for each row's individual Modifier.horizontalScroll
            // below -- confirmed by A/B testing on-device (a bare
            // LazyColumn with no scrollbar wrapper scrolls long lines
            // correctly; wrapped in LazyColumnScrollbar, horizontal
            // drags do nothing at all, even with the default Thumb-only
            // selectionMode). Known, not fixed -- see session-state.
            // The vertical scrollbar this wrapper provides is the
            // measured, decided-on feature (spike-tested, then the
            // actual jank fix); a long unwrapped line being unreadable
            // beyond the viewport is a real, currently-accepted
            // regression versus the old wrapping Text, not a silent one.
            LazyColumnScrollbar(
                state = listState,
                settings = ScrollbarSettings.Default,
                modifier = Modifier.weight(1f).fillMaxWidth(),
            ) {
                LazyColumn(state = listState, modifier = Modifier.fillMaxWidth()) {
                    items(lines) { line ->
                        // Fixed-height, non-wrapping rows -- matches
                        // Arduino IDE's own itemSize={18}/whiteSpace:
                        // nowrap choice, sidestepping LazyColumnScrollbar's
                        // own open issue #40 (non-uniform item sizes) by
                        // construction rather than gambling on it.
                        Text(
                            text = line.text,
                            fontFamily = FontFamily.Monospace,
                            softWrap = false,
                            maxLines = 1,
                            modifier = Modifier.horizontalScroll(horizontalScrollState),
                        )
                    }
                }
            }

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
                TooltipIconButton(
                    label = if (autoscroll) "Autoscroll: on" else "Autoscroll: off",
                    onClick = { autoscroll = !autoscroll },
                ) {
                    Symbol(
                        SymbolIcon.VERTICAL_ALIGN_BOTTOM,
                        contentDescription = if (autoscroll) "Autoscroll: on" else "Autoscroll: off",
                        tint = if (autoscroll) MaterialTheme.colorScheme.primary else LocalContentColor.current,
                    )
                }
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
                Button(
                    onClick = ::run,
                    enabled = status is ConnectionStatus.Connected,
                    modifier = Modifier.padding(start = 8.dp),
                ) {
                    Symbol(SymbolIcon.PLAY_ARROW, contentDescription = "Run")
                }
            }
        }
    }
}
