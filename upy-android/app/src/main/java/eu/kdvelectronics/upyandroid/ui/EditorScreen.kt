package eu.kdvelectronics.upyandroid.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.input.InputTransformation
import androidx.compose.foundation.text.input.TextFieldBuffer
import androidx.compose.foundation.text.input.TextFieldLineLimits
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.rememberTextFieldState
import androidx.compose.foundation.text.input.setTextAndPlaceCursorAtEnd
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import eu.kdvelectronics.upyandroid.managers.FilesManager
import eu.kdvelectronics.upyandroid.model.MicroFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import my.nanihadesuka.compose.ColumnScrollbar
import my.nanihadesuka.compose.ScrollbarSettings

/**
 * Code editor: run, save, undo, redo. Plain monospace text with line
 * numbers, no wrapping, no syntax highlighting.
 * Saves locally via [FilesManager], not to a remote board.
 */
// undoState is still experimental in Compose foundation.
@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun EditorScreen(
    filesManager: FilesManager,
    file: MicroFile,
    onRun: (content: String) -> Unit,
    onBack: () -> Unit
) {
    val coroutineScope = rememberCoroutineScope()
    val codeState = rememberTextFieldState()
    var loaded by remember { mutableStateOf(false) }
    var savedText by remember { mutableStateOf("") }
    var showLeave by remember { mutableStateOf(false) }
    val isDirty = codeState.text.toString() != savedText

    LaunchedEffect(file) {
        val content = withContext(Dispatchers.IO) { filesManager.read(file) }
        codeState.setTextAndPlaceCursorAtEnd(content)
        codeState.undoState.clearHistory()
        savedText = content
        loaded = true
    }

    // then() runs after the write: leaving earlier would cancel it.
    fun save(then: () -> Unit = {}) {
        val text = codeState.text.toString()
        coroutineScope.launch {
            withContext(Dispatchers.IO) { filesManager.write(file, text) }
            savedText = text
            then()
        }
    }

    fun leave() {
        if (isDirty) showLeave = true else onBack()
    }

    BackHandler(enabled = isDirty) { showLeave = true }

    if (showLeave) {
        AlertDialog(
            onDismissRequest = { showLeave = false },
            title = { Text("Save changes?") },
            confirmButton = {
                TextButton(onClick = {
                    showLeave = false
                    save { onBack() }
                }) { Text("Save") }
            },
            dismissButton = {
                Row {
                    TextButton(onClick = { showLeave = false }) { Text("Cancel") }
                    TextButton(onClick = {
                        showLeave = false
                        onBack()
                    }) { Text("Discard") }
                }
            },
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        file.name,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                },
                navigationIcon = {
                    IconButton(onClick = { leave() }) {
                        Symbol(SymbolIcon.ARROW_BACK, contentDescription = "Back")
                    }
                },
                actions = {
                    TooltipIconButton(
                        icon = SymbolIcon.UNDO,
                        label = "Undo",
                        onClick = { codeState.undoState.undo() },
                        enabled = codeState.undoState.canUndo,
                    )
                    TooltipIconButton(
                        icon = SymbolIcon.REDO,
                        label = "Redo",
                        onClick = { codeState.undoState.redo() },
                        enabled = codeState.undoState.canRedo,
                    )
                    TooltipIconButton(
                        icon = SymbolIcon.SAVE,
                        label = "Save",
                        onClick = { save() },
                        enabled = isDirty,
                    )
                    TooltipIconButton(
                        icon = SymbolIcon.PLAY_ARROW,
                        label = "Run",
                        onClick = { onRun(codeState.text.toString()) },
                    )
                }
            )
        },
    ) { padding ->
        if (loaded) {
            CodeEditor(
                state = codeState,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
            )
        }
    }
}

// Line numbers left, text right. One vertical scroll for both keeps the
// numbers in line; same style, so same line height. Horizontal scroll
// on the text only: long lines do not wrap.
@Composable
private fun CodeEditor(state: TextFieldState, modifier: Modifier = Modifier) {
    val style = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontSize = 14.sp,
        lineHeight = 20.sp,
        color = MaterialTheme.colorScheme.onSurface,
    )
    val lineCount = state.text.count { it == '\n' } + 1
    val scrollState = rememberScrollState()
    // Always shown: a swipe on the text field does not make it appear.
    ColumnScrollbar(state = scrollState, settings = ScrollbarSettings.Default.copy(alwaysShowScrollbar = true), modifier = modifier) {
        Row(Modifier.verticalScroll(scrollState).padding(8.dp)) {
            Text(
                text = (1..lineCount).joinToString("\n"),
                style = style.copy(color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.End),
                modifier = Modifier.padding(end = 8.dp),
            )
            BoxWithConstraints(Modifier.weight(1f)) {
                BasicTextField(
                    state = state,
                    modifier = Modifier.horizontalScroll(rememberScrollState()).widthIn(min = maxWidth),
                    textStyle = style,
                    inputTransformation = PythonIndent,
                    lineLimits = TextFieldLineLimits.MultiLine(),
                    keyboardOptions = KeyboardOptions(
                        capitalization = KeyboardCapitalization.None,
                        autoCorrectEnabled = false,
                    ),
                    cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
                )
            }
        }
    }
}

// Enter copies the indentation of the text before the cursor, plus 4 spaces
// after a ":" on a line without "#". Backspace in leading spaces removes back
// to the previous multiple of 4. Only single typed edits: a paste is left alone.
@OptIn(ExperimentalFoundationApi::class)
private object PythonIndent : InputTransformation {
    override fun TextFieldBuffer.transformInput() {
        if (changes.changeCount != 1 || !originalSelection.collapsed) return
        val range = changes.getRange(0)
        val original = changes.getOriginalRange(0)
        val text = asCharSequence()

        if (original.collapsed && range.length == 1 && text[range.start] == '\n') {
            val lineStart = text.lastIndexOf('\n', range.start - 1) + 1
            val line = text.substring(lineStart, range.start)
            var indent = line.takeWhile { it == ' ' || it == '\t' }
            if (line.trimEnd().endsWith(":") && '#' !in line) indent += "    "
            if (indent.isNotEmpty()) {
                replace(range.end, range.end, indent)
                selection = TextRange(range.end + indent.length)
            }
            return
        }

        // Backspace (not Delete): the cursor was right after the deleted space.
        if (range.collapsed && original.length == 1 && originalSelection.start == original.end &&
            originalText[original.start] == ' '
        ) {
            val lineStart = originalText.lastIndexOf('\n', original.start - 1) + 1
            val column = original.end - lineStart
            if ((lineStart until original.end).all { originalText[it] == ' ' }) {
                val extra = (column - 1) % 4
                if (extra > 0) {
                    replace(range.start - extra, range.start, "")
                    selection = TextRange(range.start - extra)
                }
            }
        }
    }
}
