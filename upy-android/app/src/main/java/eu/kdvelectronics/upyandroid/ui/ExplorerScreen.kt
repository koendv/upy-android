package eu.kdvelectronics.upyandroid.ui

import android.provider.OpenableColumns
import android.webkit.MimeTypeMap
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material3.AlertDialog
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import eu.kdvelectronics.upyandroid.fileprovider.openWith
import eu.kdvelectronics.upyandroid.fileprovider.shareFile
import eu.kdvelectronics.upyandroid.managers.FilesManager
import eu.kdvelectronics.upyandroid.model.MicroFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import my.nanihadesuka.compose.LazyVerticalGridScrollbar
import my.nanihadesuka.compose.ScrollbarSettings

/**
 * File explorer for the app's sandboxed storage: browse, create, rename,
 * delete, and import files and folders, run or edit a script. Reads and
 * writes locally via [FilesManager], since this app has no remote board.
 * Board storage and local scripts are the same directory, so there is no
 * separate "Scripts" screen.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ExplorerScreen(
    filesManager: FilesManager,
    onEdit: (MicroFile) -> Unit,
    onRun: (content: String) -> Unit,
    focus: String? = null,
    onFocusShown: () -> Unit = {},
) {
    val coroutineScope = rememberCoroutineScope()
    var path by remember { mutableStateOf("") }
    var files by remember { mutableStateOf(listOf<MicroFile>()) }
    var selected by remember { mutableStateOf<MicroFile?>(null) }
    var showOptions by remember { mutableStateOf(false) }
    var showNewFile by remember { mutableStateOf(false) }
    var showNewFolder by remember { mutableStateOf(false) }
    var showRename by remember { mutableStateOf(false) }
    var showDelete by remember { mutableStateOf(false) }
    val context = LocalContext.current

    fun refresh() {
        coroutineScope.launch(Dispatchers.IO) {
            val listed = filesManager.listDir(path)
            files = listed
        }
    }

    LaunchedEffect(path) { refresh() }

    // A file just shared into upy (name in the VFS root): show the root
    // and open that file's options, as if it had been tapped.
    LaunchedEffect(focus) {
        if (focus != null) {
            path = ""
            refresh()
            selected = MicroFile(name = focus, path = "", isDirectory = false)
            showOptions = true
            onFocusShown()
        }
    }

    // Peer nav-suite tab: no onBack to fall through to at the VFS root
    // any more. up() is a no-op there (tab switching is the only way to
    // leave this screen). BackHandler is only enabled below the root,
    // so a system back press at the root falls through to the
    // platform's own default behavior instead of being swallowed silently.
    fun up() {
        if (path.isNotEmpty()) {
            path = path.substringBeforeLast('/', "")
        }
    }

    BackHandler(enabled = path.isNotEmpty()) { up() }

    val importPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        val name = context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            cursor.moveToFirst()
            if (nameIndex >= 0) cursor.getString(nameIndex) else null
        } ?: return@rememberLauncherForActivityResult
        val bytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
            ?: return@rememberLauncherForActivityResult
        coroutineScope.launch(Dispatchers.IO) {
            filesManager.writeBinary(if (path.isEmpty()) name else "$path/$name", bytes)
            refresh()
        }
    }

    NameDialog(
        show = showNewFile,
        title = "New file",
        initial = "",
        onDismiss = { showNewFile = false },
        onOk = { name ->
            showNewFile = false
            val file = MicroFile(name = name, path = path, isDirectory = false)
            coroutineScope.launch(Dispatchers.IO) {
                filesManager.newFile(file)
                refresh()
            }
        }
    )

    NameDialog(
        show = showNewFolder,
        title = "New folder",
        initial = "",
        onDismiss = { showNewFolder = false },
        onOk = { name ->
            showNewFolder = false
            val file = MicroFile(name = name, path = path, isDirectory = true)
            coroutineScope.launch(Dispatchers.IO) {
                filesManager.newDirectory(file)
                refresh()
            }
        }
    )

    NameDialog(
        show = showRename,
        title = "Rename",
        initial = selected?.name.orEmpty(),
        onDismiss = { showRename = false },
        onOk = { newName ->
            showRename = false
            val file = selected ?: return@NameDialog
            coroutineScope.launch(Dispatchers.IO) {
                filesManager.rename(file, newName)
                refresh()
            }
        }
    )

    if (showDelete) {
        val file = selected
        AlertDialog(
            onDismissRequest = { showDelete = false },
            title = { Text("Delete ${file?.name}?") },
            confirmButton = {
                TextButton(onClick = {
                    showDelete = false
                    if (file != null) coroutineScope.launch(Dispatchers.IO) {
                        filesManager.remove(file)
                        refresh()
                    }
                }) { Text("Delete") }
            },
            dismissButton = {
                TextButton(onClick = { showDelete = false }) { Text("Cancel") }
            }
        )
    }

    if (showOptions) {
        val file = selected
        AlertDialog(
            onDismissRequest = { showOptions = false },
            title = { Text(file?.name.orEmpty()) },
            text = {
                Column {
                    if (file?.canRun == true) OptionRow(SymbolIcon.PLAY_ARROW, "Run", onClick = {
                        showOptions = false
                        // onRun (MainActivity's runAndShowTerminal) calls
                        // navController.navigate(), which requires the main
                        // thread. Only the file read itself needs IO. Same
                        // pattern as EditorScreen's own Run button.
                        coroutineScope.launch {
                            val content = withContext(Dispatchers.IO) { filesManager.read(file) }
                            onRun(content)
                        }
                    })
                    if (file?.isFile == true) OptionRow(SymbolIcon.EDIT, "Edit", onClick = {
                        showOptions = false
                        onEdit(file)
                    })
                    if (file?.isDirectory == true) OptionRow(SymbolIcon.FOLDER_OPEN, "Open", onClick = {
                        showOptions = false
                        path = file.fullPath
                    })
                    if (file?.isFile == true) OptionRow(SymbolIcon.OPEN_IN_NEW, "Open with", onClick = {
                        showOptions = false
                        openWith(context, file.fullPath, shareMimeType(file.name))
                    })
                    if (file?.isFile == true) OptionRow(SymbolIcon.SHARE, "Share", onClick = {
                        showOptions = false
                        shareFile(context, file.fullPath, shareMimeType(file.name))
                    })
                    OptionRow(SymbolIcon.DRIVE_FILE_RENAME_OUTLINE, "Rename", onClick = {
                        showOptions = false
                        showRename = true
                    })
                    OptionRow(SymbolIcon.DELETE, "Delete", onClick = {
                        showOptions = false
                        showDelete = true
                    })
                }
            },
            confirmButton = {
                TextButton(onClick = { showOptions = false }) { Text("Close") }
            }
        )
    }

    Scaffold(
        topBar = {
            Column {
                TopAppBar(
                    title = { Text("Files") },
                    actions = {
                        TooltipIconButton(
                            icon = SymbolIcon.DRIVE_FOLDER_UPLOAD,
                            label = "Directory up",
                            onClick = { up() },
                            enabled = path.isNotEmpty(),
                        )
                        TooltipIconButton(
                            icon = SymbolIcon.UPLOAD_FILE,
                            label = "Import",
                            onClick = { importPicker.launch("*/*") },
                        )
                        TooltipIconButton(
                            icon = SymbolIcon.NOTE_ADD,
                            label = "New file",
                            onClick = { showNewFile = true },
                        )
                        TooltipIconButton(
                            icon = SymbolIcon.CREATE_NEW_FOLDER,
                            label = "New folder",
                            onClick = { showNewFolder = true },
                        )
                        TooltipIconButton(
                            icon = SymbolIcon.REFRESH,
                            label = "Refresh",
                            onClick = { refresh() },
                        )
                    }
                )
                Text(
                    text = "/$path",
                    style = MaterialTheme.typography.labelMedium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
                )
            }
        }
    ) { padding ->
        val gridState = rememberLazyGridState()
        LazyVerticalGridScrollbar(
            state = gridState,
            settings = ScrollbarSettings.Default,
            modifier = Modifier.fillMaxSize().padding(padding),
        ) {
            LazyVerticalGrid(
                state = gridState,
                columns = GridCells.Adaptive(80.dp),
                modifier = Modifier
                    .fillMaxSize()
                    .padding(8.dp)
            ) {
                items(files.size) { i ->
                    val file = files[i]
                    FileItem(
                        file = file,
                        onClick = {
                            if (file.isDirectory) path = file.fullPath
                            else {
                                selected = file
                                showOptions = true
                            }
                        },
                        onLongClick = {
                            selected = file
                            showOptions = true
                        }
                    )
                }
            }
        }
    }
}

// Icon-left, text-right menu row for the file options dialog.
// contentDescription = null on the icon: the label text right next to
// it already carries the meaning, per Symbol.kt's own convention.
@Composable
private fun OptionRow(icon: Int, label: String, onClick: () -> Unit) {
    TextButton(onClick = onClick, modifier = Modifier.fillMaxWidth()) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.fillMaxWidth()
        ) {
            Symbol(icon, contentDescription = null, modifier = Modifier.padding(end = 12.dp))
            Text(label)
        }
    }
}

// MIME type for sharing or opening a file: Android's table by extension; .py as
// text/plain so editors and messengers accept it; else a plain binary.
private fun shareMimeType(name: String): String {
    val ext = name.substringAfterLast('.', "").lowercase()
    if (ext == "py") return "text/plain"
    return MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "application/octet-stream"
}

@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
private fun FileItem(
    file: MicroFile,
    onClick: () -> Unit,
    onLongClick: () -> Unit
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .padding(4.dp)
            .combinedClickable(onClick = onClick, onLongClick = onLongClick)
    ) {
        Symbol(
            if (file.isDirectory) SymbolIcon.FOLDER else SymbolIcon.DESCRIPTION,
            contentDescription = file.name,
            modifier = Modifier.padding(8.dp)
        )
        Text(
            text = file.name,
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}

