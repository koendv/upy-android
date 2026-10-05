package eu.kdvelectronics.upyandroid.ui

import android.content.ClipData
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.PlainTooltip
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TooltipAnchorPosition
import androidx.compose.material3.TooltipBox
import androidx.compose.material3.TooltipDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.rememberTooltipState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.platform.ClipEntry
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import kotlin.math.abs
import eu.kdvelectronics.upyandroid.MainViewModel
import eu.kdvelectronics.upyandroid.TerminalLog
import eu.kdvelectronics.upyandroid.managers.TerminalManager
import eu.kdvelectronics.upyandroid.model.ConnectionStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import my.nanihadesuka.compose.LazyColumnScrollbar
import my.nanihadesuka.compose.ScrollbarSettings

// Hand-rolled, not LazyColumnScrollbar's own RowScrollbar: that
// library's gesture handling swallows drags meant for a CHILD's own
// horizontalScroll. No fade animation, always visible when scrollable.
// Visual thickness (4dp) vs touch target (32dp) deliberately differ --
// a small hit target is a real usability problem. see session-state.
@Composable
private fun HorizontalScrollbar(state: ScrollState, modifier: Modifier = Modifier) {
    if (state.maxValue <= 0) return
    val visualHeight = 4.dp
    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(32.dp)
            .pointerInput(state) {
                detectDragGestures { change, dragAmount ->
                    change.consume()
                    val trackWidth = size.width.toFloat()
                    val contentWidth = trackWidth + state.maxValue
                    val thumbWidth = (trackWidth * (trackWidth / contentWidth))
                        .coerceAtLeast(24.dp.toPx())
                        .coerceAtMost(trackWidth)
                    // Scaled by actual thumb travel range, not 1:1 with
                    // the finger, or the thumb lags for narrow content.
                    val trackRange = (trackWidth - thumbWidth).coerceAtLeast(1f)
                    state.dispatchRawDelta(dragAmount.x * state.maxValue / trackRange)
                }
            }
            .drawWithContent {
                drawContent()
                val trackWidth = size.width
                val contentWidth = trackWidth + state.maxValue
                val thumbWidth = (trackWidth * (trackWidth / contentWidth))
                    .coerceAtLeast(24.dp.toPx())
                    .coerceAtMost(trackWidth)
                val thumbOffsetX = (trackWidth - thumbWidth) * (state.value.toFloat() / state.maxValue)
                val visualHeightPx = visualHeight.toPx()
                drawRoundRect(
                    color = Color.Gray,
                    topLeft = Offset(thumbOffsetX, (size.height - visualHeightPx) / 2),
                    size = Size(thumbWidth, visualHeightPx),
                    cornerRadius = CornerRadius(visualHeightPx / 2, visualHeightPx / 2)
                )
            }
    )
}

// LazyColumnScrollbar's own gesture handling swallows drags meant for
// content nested inside it. Claims clearly-horizontal drags at this
// PARENT, outside LazyColumnScrollbar, via PointerEventPass.Initial
// (runs before the library's own Main-pass handling ever sees the
// event); anything not claimed passes through untouched. see session-state.
private fun Modifier.interceptHorizontalDrag(state: ScrollState): Modifier = this.pointerInput(state) {
    awaitEachGesture {
        val down = awaitFirstDown(requireUnconsumed = false, pass = PointerEventPass.Initial)
        var overSlop = false
        var horizontal = false
        var accumX = 0f
        var accumY = 0f
        // Tracked manually: change.positionChange() returns 0.0 after
        // the first call under repeated Initial-pass querying.
        var lastX = down.position.x
        var lastY = down.position.y
        while (true) {
            val event = awaitPointerEvent(pass = PointerEventPass.Initial)
            val change = event.changes.firstOrNull { it.id == down.id } ?: break
            if (!change.pressed) break
            val dx = change.position.x - lastX
            val dy = change.position.y - lastY
            lastX = change.position.x
            lastY = change.position.y
            if (!overSlop) {
                accumX += dx
                accumY += dy
                if (abs(accumX) > viewConfiguration.touchSlop || abs(accumY) > viewConfiguration.touchSlop) {
                    overSlop = true
                    horizontal = abs(accumX) > abs(accumY)
                }
            }
            if (overSlop && horizontal) {
                change.consume()
                // Content-drag sign, opposite of the thumb-drag above:
                // moving left reveals content further right.
                state.dispatchRawDelta(-dx)
            }
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
    val clipboard = LocalClipboard.current
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
    val textMeasurer = rememberTextMeasurer()
    val rowTextStyle = LocalTextStyle.current.copy(fontFamily = FontFamily.Monospace)
    // Monospace: fixed advance x length: never measure() per row, that's
    // on the per-print hot path. see session-state.
    val glyphAdvancePx = remember(rowTextStyle) {
        textMeasurer.measure("0".repeat(40), rowTextStyle).size.width / 40f
    }
    // Every row shares this SAME width, not its own intrinsic width --
    // otherwise each row's layout independently overwrites the shared
    // state's maxValue, so short rows never move when dragging a long
    // one. Bounded to visible rows only (LazyColumn never measures
    // off-screen items).
    val maxVisibleLineWidthPx by remember {
        derivedStateOf {
            val maxChars = listState.layoutInfo.visibleItemsInfo.maxOfOrNull { info ->
                // getOrNull, not []: after clear() or front-eviction,
                // layoutInfo's indices can briefly exceed lines.size.
                lines.getOrNull(info.index)?.text?.length ?: 0
            } ?: 0
            (maxChars * glyphAdvancePx).toInt()
        }
    }
    LaunchedEffect(lines.isEmpty()) {
        if (lines.isEmpty()) horizontalScrollState.scrollTo(0)
    }
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
                    val statusLabel = when (status) {
                        is ConnectionStatus.Connecting -> "Connecting…"
                        is ConnectionStatus.Connected -> "Connected"
                        is ConnectionStatus.Disconnected -> "Disconnected"
                    }
                    TooltipBox(
                        positionProvider = TooltipDefaults.rememberTooltipPositionProvider(TooltipAnchorPosition.Below),
                        tooltip = { PlainTooltip { Text(statusLabel) } },
                        state = rememberTooltipState(),
                        modifier = Modifier.padding(end = 16.dp),
                    ) {
                        Symbol(
                            codepoint = if (status is ConnectionStatus.Connected) SymbolIcon.LINK else SymbolIcon.LINK_OFF,
                            contentDescription = statusLabel,
                        )
                    }
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
            // interceptHorizontalDrag on this OUTER Box, not inside
            // LazyColumnScrollbar's own content -- see that function's
            // own comment for why the wrapping/pass order matters.
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .interceptHorizontalDrag(horizontalScrollState),
            ) {
                LazyColumnScrollbar(
                    state = listState,
                    settings = ScrollbarSettings.Default,
                    modifier = Modifier.fillMaxSize(),
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
                                // horizontalScroll FIRST (outer), width()
                                // SECOND (inner): width() then constrains
                                // what horizontalScroll's own child (this
                                // Text) reports, not the viewport -- so
                                // every row reports the SAME size
                                // regardless of its own string length,
                                // see maxVisibleLineWidthPx's own comment.
                                modifier = Modifier
                                    .horizontalScroll(horizontalScrollState)
                                    .width(with(LocalDensity.current) { maxVisibleLineWidthPx.toDp() }),
                            )
                        }
                    }
                }
            }
            // maxValue only updates when a row is laid out, so it goes stale after clear().
            if (lines.isNotEmpty()) {
                HorizontalScrollbar(state = horizontalScrollState, modifier = Modifier.padding(top = 2.dp))
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
                    icon = SymbolIcon.CONTENT_COPY,
                    label = "Copy",
                    // Whole buffer, no truncation -- user's own call.
                    // Line.text excludes its trailing \n (see
                    // TerminalLog.kt), joinToString adds it back.
                    onClick = {
                        val text = lines.joinToString("\n") { it.text }
                        coroutineScope.launch {
                            clipboard.setClipEntry(ClipEntry(ClipData.newPlainText("terminal", text)))
                        }
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
