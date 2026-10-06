package eu.kdvelectronics.upyandroid.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.input.TextFieldLineLimits
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.rememberTextFieldState
import androidx.compose.foundation.text.input.setTextAndPlaceCursorAtEnd
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
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
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import eu.kdvelectronics.upyandroid.managers.FilesManager
import eu.kdvelectronics.upyandroid.model.MicroFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Code editor: run, save, undo, redo. Plain monospace text with line
 * numbers, no wrapping, no syntax highlighting.
 * Saves locally via [FilesManager], not to a remote board.
 *
 * @param file The file being edited, or null for a new/blank script.
 * @param path The directory a new file should be saved into (used only when [file] is null).
 */
// undoState is still experimental in Compose foundation.
@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun EditorScreen(
    filesManager: FilesManager,
    file: MicroFile?,
    path: String,
    onRun: (content: String) -> Unit,
    onBack: () -> Unit
) {
    val coroutineScope = rememberCoroutineScope()
    val codeState = rememberTextFieldState()
    var loaded by remember { mutableStateOf(file == null) }
    var savedText by remember { mutableStateOf("") }
    var showSaveAs by remember { mutableStateOf(false) }
    val isDirty = codeState.text.toString() != savedText

    LaunchedEffect(file) {
        if (file != null) {
            val content = withContext(Dispatchers.IO) { filesManager.read(file) }
            codeState.setTextAndPlaceCursorAtEnd(content)
            codeState.undoState.clearHistory()
            savedText = content
            loaded = true
        }
    }

    fun doSave(target: MicroFile) {
        coroutineScope.launch(Dispatchers.IO) {
            val text = codeState.text.toString()
            filesManager.write(target, text)
            savedText = text
        }
    }

    fun save() {
        if (file != null) doSave(file) else showSaveAs = true
    }

    NameDialog(
        show = showSaveAs,
        title = "Save as",
        initial = "main.py",
        onDismiss = { showSaveAs = false },
        onOk = { name ->
            showSaveAs = false
            doSave(MicroFile(name = name, path = path, isDirectory = false))
        }
    )

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(file?.name ?: "untitled") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Symbol(SymbolIcon.ARROW_BACK, contentDescription = "Back")
                    }
                },
                actions = {
                    IconButton(onClick = { codeState.undoState.undo() }, enabled = codeState.undoState.canUndo) {
                        Symbol(SymbolIcon.UNDO, contentDescription = "Undo")
                    }
                    IconButton(onClick = { codeState.undoState.redo() }, enabled = codeState.undoState.canRedo) {
                        Symbol(SymbolIcon.REDO, contentDescription = "Redo")
                    }
                }
            )
        },
        bottomBar = {
            Row(modifier = Modifier.fillMaxWidth().padding(8.dp)) {
                TextButton(onClick = { onRun(codeState.text.toString()) }) { Text("Run") }
                TextButton(onClick = { save() }) {
                    Text(if (isDirty) "Save*" else "Save")
                }
            }
        }
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
    Row(modifier.verticalScroll(rememberScrollState()).padding(8.dp)) {
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
