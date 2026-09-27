package eu.kdvelectronics.upyandroid

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.isImeVisible
import androidx.compose.material3.Text
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfoV2
import androidx.compose.material3.adaptive.navigationsuite.ExperimentalMaterial3AdaptiveNavigationSuiteApi
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteScaffold
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteScaffoldDefaults
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteType
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.core.content.ContextCompat
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import eu.kdvelectronics.upyandroid.fileprovider.shareFile
import eu.kdvelectronics.upyandroid.http.HttpServerManager
import eu.kdvelectronics.upyandroid.managers.BoardManager
import eu.kdvelectronics.upyandroid.managers.FilesManager
import eu.kdvelectronics.upyandroid.managers.SettingsManager
import eu.kdvelectronics.upyandroid.managers.TerminalManager
import eu.kdvelectronics.upyandroid.model.MicroFile
import eu.kdvelectronics.upyandroid.ssh.SshServerManager
import eu.kdvelectronics.upyandroid.ui.AboutScreen
import eu.kdvelectronics.upyandroid.ui.CameraScreen
import eu.kdvelectronics.upyandroid.ui.EditorScreen
import eu.kdvelectronics.upyandroid.ui.ExplorerScreen
import eu.kdvelectronics.upyandroid.ui.SettingsScreen
import eu.kdvelectronics.upyandroid.ui.Symbol
import eu.kdvelectronics.upyandroid.ui.SymbolIcon
import eu.kdvelectronics.upyandroid.ui.TerminalScreen
import eu.kdvelectronics.upyandroid.ui.UpyTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

// Four peer nav destinations, in the order they appear in the nav
// suite -- Editor is deliberately NOT here, it's a non-peer detail
// screen reached only via Explorer (see MainActivity's own NavHost).
private enum class TopLevelDestination(val route: String, val label: String, val icon: Int) {
    TERMINAL("terminal", "Command", SymbolIcon.TERMINAL),
    EXPLORER("explorer", "Files", SymbolIcon.FOLDER),
    CAMERA("camera", "Camera", SymbolIcon.CAMERA),
    SETTINGS("settings", "Settings", SymbolIcon.SETTINGS),
}

// Standard "switch peer tab" navigation: preserves each tab's own back
// stack/scroll position across switches (saveState/restoreState), and
// never piles up duplicate destinations on repeated taps of the same
// tab (launchSingleTop). Used by every nav-suite item's onClick AND by
// runAndShowTerminal() below (switching to the Command tab after a
// script starts is the same kind of tab switch, not a new destination
// on top of the stack).
private fun NavHostController.navigateToTab(route: String) {
    navigate(route) {
        popUpTo(graph.findStartDestination().id) { saveState = true }
        launchSingleTop = true
        restoreState = true
    }
}

// Binds to EngineService (a separate :engine process) via BoardManager
// and talks to it only through TerminalManager, never touching
// Engine/JNI directly. Three screens (terminal, explorer, editor) via
// Navigation Compose. Explorer and editor never touch the engine
// process directly either: FilesManager does plain local java.io.File
// I/O rooted at the same filesDir the :engine process mounts as VFS
// "/", so browsing and editing never need AIDL. Only "Run" does, via
// the same terminalManager.eval() the terminal screen itself uses.
@OptIn(ExperimentalMaterial3AdaptiveNavigationSuiteApi::class, ExperimentalLayoutApi::class)
class MainActivity : ComponentActivity() {
    private lateinit var boardManager: BoardManager
    private lateinit var terminalManager: TerminalManager
    private lateinit var filesManager: FilesManager
    private lateinit var settingsManager: SettingsManager
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
        // HttpServerManager.kt's own header comment. A process-wide
        // singleton (not a per-Activity instance): deliberately never
        // stopped in onDestroy(), like AdbExecProvider's own
        // BoardManager, so it should keep serving for as long as this
        // process is alive, not just while MainActivity itself is on
        // screen -- including across an Activity recreation triggered
        // by a config change (e.g. a system theme switch), which a
        // per-Activity instance got wrong (see its own header comment).
        HttpServerManager.applySettings(applicationContext, settingsManager)
        // Same lifecycle reasoning as HttpServerManager above -- also
        // never stopped in onDestroy(), also a process-wide singleton.
        SshServerManager.applySettings(applicationContext, settingsManager)
        boardManager.connect()
        maybeRequestCameraPermission()

        setContent {
            UpyTheme {
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
                    navController.navigateToTab(TopLevelDestination.TERMINAL.route)
                }

                val backStackEntry by navController.currentBackStackEntryAsState()
                val currentRoute = backStackEntry?.destination?.route
                // Real, on-device layout risk (flagged, not guessed): the
                // terminal already stacks output + action row + input
                // field under imePadding() -- a persistent nav band would
                // be a fourth competing band while the soft keyboard is
                // open. Hidden outright while the IME is visible, on
                // every tab, not just the terminal's own.
                val imeVisible = WindowInsets.isImeVisible
                val layoutType = if (imeVisible) {
                    NavigationSuiteType.None
                } else {
                    NavigationSuiteScaffoldDefaults.calculateFromAdaptiveInfo(currentWindowAdaptiveInfoV2())
                }

                NavigationSuiteScaffold(
                    navigationSuiteItems = {
                        TopLevelDestination.entries.forEach { destination ->
                            item(
                                selected = currentRoute == destination.route,
                                onClick = { navController.navigateToTab(destination.route) },
                                icon = {
                                    Symbol(destination.icon, contentDescription = null)
                                },
                                label = { Text(destination.label) },
                            )
                        }
                    },
                    layoutType = layoutType,
                ) {
                    NavHost(navController = navController, startDestination = TopLevelDestination.TERMINAL.route) {
                        composable(TopLevelDestination.TERMINAL.route) {
                            TerminalScreen(
                                viewModel = vm,
                                terminalManager = terminalManager,
                                status = status,
                                onReconnect = { boardManager.connect() },
                            )
                        }
                        composable(TopLevelDestination.SETTINGS.route) {
                            SettingsScreen(
                                settingsManager = settingsManager,
                                onSettingsChanged = {
                                    boardManager.pushSettings()
                                    HttpServerManager.applySettings(applicationContext, settingsManager)
                                    SshServerManager.applySettings(applicationContext, settingsManager)
                                },
                                onOpenAbout = { navController.navigate("about") },
                            )
                        }
                        composable(TopLevelDestination.CAMERA.route) {
                            CameraScreen(boardManager = boardManager)
                        }
                        composable(TopLevelDestination.EXPLORER.route) {
                            ExplorerScreen(
                                filesManager = filesManager,
                                onEdit = { file, path ->
                                    pendingFile.value = file
                                    pendingPath.value = path
                                    navController.navigate("editor")
                                },
                                onRun = { content -> runAndShowTerminal(content) },
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
                        composable("about") {
                            AboutScreen(onBack = { navController.popBackStack() })
                        }
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
