package eu.kdvelectronics.upyandroid.ui

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.ui.unit.dp
import eu.kdvelectronics.upyandroid.managers.FilesManager
import eu.kdvelectronics.upyandroid.model.MicroFile
import io.ma7moud3ly.nemo.NemoCodeEditor
import io.ma7moud3ly.nemo.model.Language
import io.ma7moud3ly.nemo.model.rememberCodeState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Code editor -- same functionality as micro-repl's own editor (run /
 * save / new / undo / redo, syntax highlighting via the same nemo-editor
 * library micro-repl itself depends on -- MIT, see NOTICE.md) minus its
 * theme picker (dropped, out of scope for now -- see SESSION_STATE.yaml).
 * Saves locally via [FilesManager], not to a remote board.
 *
 * @param file The file being edited, or null for a new/blank script.
 * @param path The directory a new file should be saved into (used only when [file] is null).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EditorScreen(
    filesManager: FilesManager,
    file: MicroFile?,
    path: String,
    onRun: (content: String) -> Unit,
    onBack: () -> Unit
) {
    val coroutineScope = rememberCoroutineScope()
    val codeState = rememberCodeState(code = "", language = Language.PYTHON)
    var loaded by remember { mutableStateOf(file == null) }
    var savedText by remember { mutableStateOf("") }
    var showSaveAs by remember { mutableStateOf(false) }
    val isDirty = codeState.code != savedText

    LaunchedEffect(file) {
        if (file != null) {
            val content = withContext(Dispatchers.IO) { filesManager.read(file) }
            codeState.updateText(content)
            savedText = content
            loaded = true
        }
    }

    fun doSave(target: MicroFile) {
        coroutineScope.launch(Dispatchers.IO) {
            filesManager.write(target, codeState.code)
            savedText = codeState.code
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
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    IconButton(onClick = { codeState.undo() }) {
                        Icon(Icons.AutoMirrored.Filled.Undo, contentDescription = "Undo")
                    }
                    IconButton(onClick = { codeState.redo() }) {
                        Icon(Icons.AutoMirrored.Filled.Redo, contentDescription = "Redo")
                    }
                }
            )
        },
        bottomBar = {
            Row(modifier = Modifier.fillMaxWidth().padding(8.dp)) {
                TextButton(onClick = { onRun(codeState.code) }) { Text("Run") }
                TextButton(onClick = { save() }) {
                    Text(if (isDirty) "Save*" else "Save")
                }
            }
        }
    ) { padding ->
        if (loaded) {
            NemoCodeEditor(
                state = codeState,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
            )
        }
    }
}
