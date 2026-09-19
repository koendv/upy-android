package eu.kdvelectronics.upyandroid.ui

import android.provider.OpenableColumns
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
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CreateNewFolder
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.UploadFile
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import eu.kdvelectronics.upyandroid.managers.FilesManager
import eu.kdvelectronics.upyandroid.model.MicroFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

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
    onEdit: (MicroFile?, path: String) -> Unit,
    onRun: (content: String) -> Unit,
    onBack: () -> Unit
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

    fun up() {
        if (path.isEmpty()) {
            onBack()
        } else {
            path = path.substringBeforeLast('/', "")
        }
    }

    BackHandler { up() }

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
                    if (file?.canRun == true) TextButton(onClick = {
                        showOptions = false
                        // onRun (MainActivity's runAndShowTerminal) calls
                        // navController.navigate(), which requires the main
                        // thread. Only the file read itself needs IO. Same
                        // pattern as EditorScreen's own Run button.
                        coroutineScope.launch {
                            val content = withContext(Dispatchers.IO) { filesManager.read(file) }
                            onRun(content)
                        }
                    }) { Text("Run") }
                    if (file?.isFile == true) TextButton(onClick = {
                        showOptions = false
                        onEdit(file, path)
                    }) { Text("Edit") }
                    if (file?.isDirectory == true) TextButton(onClick = {
                        showOptions = false
                        path = file.fullPath
                    }) { Text("Open") }
                    TextButton(onClick = {
                        showOptions = false
                        showRename = true
                    }) { Text("Rename") }
                    TextButton(onClick = {
                        showOptions = false
                        showDelete = true
                    }) { Text("Delete") }
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
                        IconButton(onClick = { importPicker.launch("*/*") }) {
                            Icon(Icons.Filled.UploadFile, contentDescription = "Import")
                        }
                        IconButton(onClick = { showNewFile = true }) {
                            Icon(Icons.Filled.Add, contentDescription = "New file")
                        }
                        IconButton(onClick = { showNewFolder = true }) {
                            Icon(Icons.Filled.CreateNewFolder, contentDescription = "New folder")
                        }
                        IconButton(onClick = { refresh() }) {
                            Icon(Icons.Filled.Refresh, contentDescription = "Refresh")
                        }
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
        LazyVerticalGrid(
            columns = GridCells.Adaptive(80.dp),
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
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
        Icon(
            imageVector = if (file.isDirectory) Icons.Filled.Folder else Icons.Filled.Description,
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

