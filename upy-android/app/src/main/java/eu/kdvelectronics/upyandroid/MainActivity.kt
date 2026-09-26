package eu.kdvelectronics.upyandroid

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import eu.kdvelectronics.upyandroid.managers.SettingsManager
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import eu.kdvelectronics.upyandroid.fileprovider.shareFile
import eu.kdvelectronics.upyandroid.http.HttpServerManager
import eu.kdvelectronics.upyandroid.managers.BoardManager
import eu.kdvelectronics.upyandroid.managers.FilesManager
import eu.kdvelectronics.upyandroid.managers.TerminalManager
import eu.kdvelectronics.upyandroid.model.MicroFile
import eu.kdvelectronics.upyandroid.ssh.SshServerManager
import eu.kdvelectronics.upyandroid.ui.CameraScreen
import eu.kdvelectronics.upyandroid.ui.EditorScreen
import eu.kdvelectronics.upyandroid.ui.ExplorerScreen
import eu.kdvelectronics.upyandroid.ui.SettingsScreen
import eu.kdvelectronics.upyandroid.ui.TerminalScreen
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

// Binds to EngineService (a separate :engine process) via BoardManager
// and talks to it only through TerminalManager, never touching
// Engine/JNI directly. Three screens (terminal, explorer, editor) via
// Navigation Compose. Explorer and editor never touch the engine
// process directly either: FilesManager does plain local java.io.File
// I/O rooted at the same filesDir the :engine process mounts as VFS
// "/", so browsing and editing never need AIDL. Only "Run" does, via
// the same terminalManager.eval() the terminal screen itself uses.
class MainActivity : ComponentActivity() {
    private lateinit var boardManager: BoardManager
    private lateinit var terminalManager: TerminalManager
    private lateinit var filesManager: FilesManager
    private lateinit var settingsManager: SettingsManager
    private lateinit var httpServerManager: HttpServerManager
    private lateinit var sshServerManager: SshServerManager
    private lateinit var viewModel: MainViewModel

    // Must be registered unconditionally before STARTED (Android's own
    // requirement for ActivityResultContracts), so this is a field, not
    // something created lazily inside onCreate. The callback is a no-op:
    // whether the user granted or denied, camera_module.cpp's own
    // OSError("camera access denied...") remains the actual runtime
    // signal a script sees on next csi.reset(). This launcher only
    // covers surfacing the OS's real grant dialog once, automatically.
    private val cameraPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        viewModel = MainViewModel()
        boardManager = BoardManager(this) { status ->
            viewModel.status.value = status
        }
        // Registered once, globally, rather than per-screen: every
        // exec() (terminal input, or explorer/editor "Run") shows up in
        // the terminal's own output, so a single listener forwarding
        // straight to TerminalLog covers every caller. Re-applied to
        // the engine automatically on every BoardManager (re)connect.
        boardManager.setOutputListener { chunk -> TerminalLog.append(chunk) }
        // android.fileprovider.share() requests, routed here (the
        // main/UI process) from :engine -- see the plan's own Part 7
        // cross-process constraint. this is a real Activity Context,
        // required by FileProviderShim.shareFile()'s startActivity().
        boardManager.setShareRequestListener { path, mimeType -> shareFile(this, path, mimeType) }
        terminalManager = TerminalManager(boardManager)
        filesManager = FilesManager(filesDir)
        settingsManager = SettingsManager(this)
        // Lives in this (default/UI) process, not :engine -- see
        // HttpServerManager.kt's own header comment. Deliberately never
        // stopped in onDestroy(): like AdbExecProvider's own BoardManager,
        // it should keep serving for as long as this process is alive,
        // not just while MainActivity itself is on screen.
        httpServerManager = HttpServerManager(applicationContext, settingsManager)
        httpServerManager.applySettings()
        // Same lifecycle reasoning as httpServerManager above -- also
        // never stopped in onDestroy().
        sshServerManager = SshServerManager(applicationContext, settingsManager)
        sshServerManager.applySettings()
        boardManager.connect()
        maybeRequestCameraPermission()

        setContent {
            MaterialTheme {
                val status by viewModel.status.collectAsState()
                val vm = remember { viewModel }
                val navController = rememberNavController()
                val coroutineScope = rememberCoroutineScope()

                // Routes carry no script data; it is handed over through
                // these variables instead.
                var pendingFile = remember { mutableStateOf<MicroFile?>(null) }
                var pendingPath = remember { mutableStateOf("") }

                fun runAndShowTerminal(content: String) {
                    TerminalLog.append("\n>>> (running script)\n")
                    // Output arrives live via the output listener
                    // registered above, not from eval()'s return value.
                    coroutineScope.launch(Dispatchers.IO) {
                        terminalManager.eval(content)
                    }
                    navController.navigate("terminal") {
                        popUpTo("terminal") { inclusive = true }
                    }
                }

                NavHost(navController = navController, startDestination = "terminal") {
                    composable("terminal") {
                        TerminalScreen(
                            viewModel = vm,
                            terminalManager = terminalManager,
                            status = status,
                            onReconnect = { boardManager.connect() },
                            onOpenFiles = { navController.navigate("explorer") },
                            onOpenCamera = { navController.navigate("camera") },
                            onOpenSettings = { navController.navigate("settings") }
                        )
                    }
                    composable("settings") {
                        SettingsScreen(
                            settingsManager = settingsManager,
                            onSettingsChanged = {
                                boardManager.pushSettings()
                                httpServerManager.applySettings()
                                sshServerManager.applySettings()
                            },
                            onBack = { navController.popBackStack() }
                        )
                    }
                    composable("camera") {
                        CameraScreen(
                            boardManager = boardManager,
                            onBack = { navController.popBackStack() }
                        )
                    }
                    composable("explorer") {
                        ExplorerScreen(
                            filesManager = filesManager,
                            onEdit = { file, path ->
                                pendingFile.value = file
                                pendingPath.value = path
                                navController.navigate("editor")
                            },
                            onRun = { content -> runAndShowTerminal(content) },
                            onBack = { navController.popBackStack() }
                        )
                    }
                    composable("editor") {
                        EditorScreen(
                            filesManager = filesManager,
                            file = pendingFile.value,
                            path = pendingPath.value,
                            onRun = { content -> runAndShowTerminal(content) },
                            onBack = { navController.popBackStack() }
                        )
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        boardManager.disconnect()
    }

    // Android cannot distinguish "never asked" from "permanently
    // denied": shouldShowRequestPermissionRationale() is false in both
    // cases. This tracks "has this app ever asked" itself and prompts
    // exactly once ever. camera_module.cpp's own OSError remains the
    // fallback signal on denial, telling the user to grant access
    // manually via Settings.
    // see session-state: MainActivity.kt#maybeRequestCameraPermission
    private fun maybeRequestCameraPermission() {
        val granted = ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) ==
            PackageManager.PERMISSION_GRANTED
        if (granted || settingsManager.askedCameraPermission) return

        settingsManager.askedCameraPermission = true
        cameraPermissionLauncher.launch(Manifest.permission.CAMERA)
    }
}
