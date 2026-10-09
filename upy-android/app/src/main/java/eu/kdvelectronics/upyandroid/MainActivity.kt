package eu.kdvelectronics.upyandroid

import android.Manifest
import android.content.ContentResolver
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.Surface
import android.webkit.MimeTypeMap
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
import androidx.compose.runtime.LaunchedEffect
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import eu.kdvelectronics.upyandroid.fileprovider.shareFile
import eu.kdvelectronics.upyandroid.http.HttpServerManager
import eu.kdvelectronics.upyandroid.managers.FilesManager
import eu.kdvelectronics.upyandroid.managers.SettingsManager
import eu.kdvelectronics.upyandroid.managers.TerminalManager
import eu.kdvelectronics.upyandroid.model.MicroFile
import eu.kdvelectronics.upyandroid.ssh.SshServerManager
import eu.kdvelectronics.upyandroid.ui.AboutScreen
import eu.kdvelectronics.upyandroid.ui.AttributionsScreen
import eu.kdvelectronics.upyandroid.ui.LicensesScreen
import eu.kdvelectronics.upyandroid.ui.CameraScreen
import eu.kdvelectronics.upyandroid.ui.EditorScreen
import eu.kdvelectronics.upyandroid.ui.ExplorerScreen
import eu.kdvelectronics.upyandroid.ui.SettingsScreen
import eu.kdvelectronics.upyandroid.ui.Symbol
import eu.kdvelectronics.upyandroid.ui.SymbolIcon
import eu.kdvelectronics.upyandroid.ui.TerminalScreen
import eu.kdvelectronics.upyandroid.ui.UpyTheme
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// Four peer nav destinations, in the order they appear in the nav
// suite. Editor is deliberately not here, it's a non-peer detail
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

// Binds to EngineService (a separate :engine process) via
// ScriptExecCore's shared BoardManager connection and talks to it only
// through TerminalManager, never touching Engine/JNI directly. Three
// screens (terminal, explorer, editor) via
// Navigation Compose. Explorer and editor never touch the engine
// process directly either: FilesManager does plain local java.io.File
// I/O rooted at the same filesDir the :engine process mounts as VFS
// "/", so browsing and editing never need AIDL. Only "Run" does, via
// the same terminalManager.eval() the terminal screen itself uses.
// Bumped whenever a bundled demo script's own content changes. See
// seedDemoScriptsIfNeeded() below. Lets an app update that fixes a
// demo script reach existing installs too, not just fresh ones.
private const val CURRENT_DEMO_SCRIPTS_VERSION = 15

// Bumped whenever the bundled /rom/ files change. See seedRomIfNeeded().
private const val CURRENT_ROM_VERSION = 1

@OptIn(ExperimentalMaterial3AdaptiveNavigationSuiteApi::class, ExperimentalLayoutApi::class)
class MainActivity : ComponentActivity() {
    private lateinit var terminalManager: TerminalManager
    private lateinit var filesManager: FilesManager
    private lateinit var settingsManager: SettingsManager
    private lateinit var viewModel: MainViewModel

    // Must be registered unconditionally before STARTED (Android's own
    // requirement for ActivityResultContracts), so this is a field, not
    // something created lazily inside onCreate. The callback is a no-op:
    // whether the user granted or denied, the camera module's own
    // OSError(EACCES) remains the actual runtime signal a script sees
    // on its next camera.Camera(). This launcher only
    // covers surfacing the OS's real grant dialog once, automatically.
    private val cameraPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { }

    // Prompts requested by a script (android.location.start()). The
    // result needs no handling: the script checks the permission itself
    // when it runs again.
    private val scriptPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { }

    // Name (in the VFS root) of the first file saved from a share; the
    // Files screen opens on it, then clears this.
    private val sharedFocus = mutableStateOf<String?>(null)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        viewModel = MainViewModel()
        // Output-listener forwarding into TerminalLog is registered
        // once, inside ScriptExecCore itself (not here), so it covers
        // every caller -- terminal input, explorer/editor "Run",
        // adb-exec, and SSH -- through the one shared connection. See
        // ScriptExecCore.kt#ScriptExecCore.
        //
        // android.fileprovider.share() requests, routed here (the
        // main/UI process) from :engine. This is a real Activity Context,
        // required by FileProviderShim.shareFile()'s startActivity().
        // Cleared in onDestroy() -- see its own comment.
        ScriptExecCore.setShareRequestListener { path, mimeType -> shareFile(this, path, mimeType) }
        // Called on a Binder thread; the launcher must run on the main thread.
        ScriptExecCore.setPermissionRequestListener { permissions ->
            runOnUiThread { scriptPermissionLauncher.launch(permissions) }
        }
        terminalManager = TerminalManager(applicationContext)
        filesManager = FilesManager(filesDir)
        settingsManager = SettingsManager(this)
        seedDemoScriptsIfNeeded()
        seedRomIfNeeded()
        // Lives in this (default/UI) process, not :engine. See
        // HttpServerManager.kt's own header comment. A process-wide
        // singleton (not a per-Activity instance): deliberately never
        // stopped in onDestroy(), like AdbExecProvider's own
        // BoardManager, so it should keep serving for as long as this
        // process is alive, not just while MainActivity itself is on
        // screen, including across an Activity recreation triggered
        // by a config change (e.g. a system theme switch), which a
        // per-Activity instance got wrong (see its own header comment).
        HttpServerManager.applySettings(applicationContext, settingsManager)
        // Same lifecycle reasoning as HttpServerManager above. Also
        // never stopped in onDestroy(), also a process-wide singleton.
        SshServerManager.applySettings(applicationContext, settingsManager)
        ScriptExecCore.connect(applicationContext)
        maybeRequestCameraPermission()
        lifecycleScope.launch {
            ScriptExecCore.cameraRotation.collect { requestedOrientation = screenOrientation(it) }
        }
        // Not on recreation (rotation, theme): the same share intent
        // would be saved a second time.
        if (savedInstanceState == null) receiveShare(intent)

        setContent {
            UpyTheme {
                val status by ScriptExecCore.status.collectAsState()
                val vm = remember { viewModel }
                val navController = rememberNavController()
                val coroutineScope = rememberCoroutineScope()

                // Routes carry no script data; it is handed over through
                // these variables instead.
                var pendingFile = remember { mutableStateOf<MicroFile?>(null) }

                val focus = sharedFocus.value
                LaunchedEffect(focus) {
                    if (focus != null) navController.navigateToTab(TopLevelDestination.EXPLORER.route)
                }

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
                // field under imePadding(). A persistent nav band would
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
                                onReconnect = { ScriptExecCore.connect(applicationContext) },
                            )
                        }
                        composable(TopLevelDestination.SETTINGS.route) {
                            SettingsScreen(
                                settingsManager = settingsManager,
                                onSettingsChanged = {
                                    ScriptExecCore.pushSettings()
                                    HttpServerManager.applySettings(applicationContext, settingsManager)
                                    SshServerManager.applySettings(applicationContext, settingsManager)
                                },
                                onOpenAbout = { navController.navigate("about") },
                            )
                        }
                        composable(TopLevelDestination.CAMERA.route) {
                            CameraScreen()
                        }
                        composable(TopLevelDestination.EXPLORER.route) {
                            ExplorerScreen(
                                filesManager = filesManager,
                                onEdit = { file ->
                                    pendingFile.value = file
                                    navController.navigate("editor")
                                },
                                onRun = { content -> runAndShowTerminal(content) },
                                focus = focus,
                                onFocusShown = { sharedFocus.value = null },
                            )
                        }
                        composable("editor") {
                            pendingFile.value?.let { file ->
                                EditorScreen(
                                    filesManager = filesManager,
                                    file = file,
                                    onRun = { content -> runAndShowTerminal(content) },
                                    onBack = { navController.popBackStack() }
                                )
                            }
                        }
                        composable("about") {
                            AboutScreen(
                                onBack = { navController.popBackStack() },
                                onOpenAttributions = { navController.navigate("attributions") },
                                onOpenLicenses = { navController.navigate("licenses") },
                            )
                        }
                        composable("attributions") {
                            AttributionsScreen(onBack = { navController.popBackStack() })
                        }
                        composable("licenses") {
                            LicensesScreen(onBack = { navController.popBackStack() })
                        }
                    }
                }
            }
        }
    }

    // singleTask: a share while upy is running arrives here.
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        receiveShare(intent)
    }

    // Share target (ACTION_SEND / ACTION_SEND_MULTIPLE, any type): saves
    // each shared file, or shared text as shared.py, in the VFS root,
    // then opens the Files screen on the first one. Runs in this UI
    // process, so it works while the engine is busy or disconnected.
    private fun receiveShare(intent: Intent?) {
        intent ?: return
        val uris: List<Uri> = when (intent.action) {
            Intent.ACTION_SEND -> listOfNotNull(IntentCompat.getParcelableExtra(intent, Intent.EXTRA_STREAM, Uri::class.java))
            Intent.ACTION_SEND_MULTIPLE ->
                IntentCompat.getParcelableArrayListExtra(intent, Intent.EXTRA_STREAM, Uri::class.java).orEmpty()
            else -> return
        }
        val text = if (uris.isEmpty()) intent.getCharSequenceExtra(Intent.EXTRA_TEXT)?.toString() else null
        lifecycleScope.launch {
            val saved = withContext(Dispatchers.IO) {
                if (text != null) {
                    listOf(saveShared("shared.py") { it.write(text.toByteArray()) })
                } else {
                    uris.filter { it.scheme == ContentResolver.SCHEME_CONTENT }.mapNotNull { uri ->
                        try {
                            val input = contentResolver.openInputStream(uri) ?: return@mapNotNull null
                            input.use { saveShared(sharedName(uri)) { out -> input.copyTo(out) } }
                        } catch (e: Exception) {
                            null // unreadable (revoked, removed): skip it, save the rest
                        }
                    }
                }
            }
            saved.firstOrNull()?.let { sharedFocus.value = it }
        }
    }

    // The sender's display name without any directory part, else
    // "shared" plus an extension from the MIME type.
    private fun sharedName(uri: Uri): String {
        val display = try {
            contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0) else null
            }
        } catch (e: Exception) {
            null
        }
        val name = display?.substringAfterLast('/')?.substringAfterLast('\\')?.trim()
        if (!name.isNullOrEmpty() && name != "." && name != "..") return name
        val ext = contentResolver.getType(uri)?.let { MimeTypeMap.getSingleton().getExtensionFromMimeType(it) } ?: "bin"
        return "shared.$ext"
    }

    // Writes a new file in the VFS root; an existing name becomes
    // name_1.ext, name_2.ext, ... Returns the name used.
    private fun saveShared(name: String, write: (java.io.OutputStream) -> Unit): String {
        val dot = name.lastIndexOf('.').takeIf { it > 0 } ?: name.length
        val base = name.substring(0, dot)
        val ext = name.substring(dot)
        var candidate = name
        var n = 1
        while (File(filesDir, candidate).exists()) {
            candidate = "${base}_$n$ext"
            n++
        }
        File(filesDir, candidate).outputStream().use(write)
        return candidate
    }

    override fun onDestroy() {
        super.onDestroy()
        // :engine intentionally outlives MainActivity now -- adb-exec
        // and SSH keep working after this Activity is destroyed
        // (including a config-change recreation, not just the user
        // leaving), and process death is what actually tears the
        // connection down, not onDestroy(). See
        // ScriptExecCore.kt#ScriptExecCore. Only the Activity-scoped
        // share-request lambda (closes over this Activity) gets
        // cleared here, so a destroyed/recreated Activity never leaves
        // a stale Activity reference wired into the process-lifetime
        // ScriptExecCore singleton.
        ScriptExecCore.setShareRequestListener(null)
        ScriptExecCore.setPermissionRequestListener(null)
    }

    // Screen orientation showing the display in Surface.ROTATION_*
    // rotation, for the screen lock while a camera is open. Display
    // rotation is relative to the device's natural orientation, so the
    // mapping differs for landscape-natural devices (tablets; untested).
    private fun screenOrientation(rotation: Int): Int {
        if (rotation < 0) return ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        val current = ContextCompat.getDisplayOrDefault(this).rotation
        val portrait = resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT
        val naturalPortrait = portrait == (current == Surface.ROTATION_0 || current == Surface.ROTATION_180)
        return if (naturalPortrait) {
            when (rotation) {
                Surface.ROTATION_90 -> ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                Surface.ROTATION_180 -> ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT
                Surface.ROTATION_270 -> ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE
                else -> ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
            }
        } else {
            when (rotation) {
                Surface.ROTATION_90 -> ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                Surface.ROTATION_180 -> ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE
                Surface.ROTATION_270 -> ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT
                else -> ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
        }
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

    // Bundled demo scripts (build.gradle.kts's own copyDemoScripts task
    // copies them from the repo's own tracked examples/ into
    // src/main/assets/examples/) seeded into the VFS's own /examples/
    // subdirectory. A dedicated, clearly app-managed location, never
    // VFS root, so this can never collide with or overwrite anything
    // the user creates themselves. Version-stamped, not a one-shot
    // boolean (see demoScriptsVersion's own comment): re-seeds (only
    // this subdirectory, only these known filenames) whenever the
    // bundled version is newer than what's already been seeded.
    private fun seedDemoScriptsIfNeeded() {
        if (settingsManager.demoScriptsVersion >= CURRENT_DEMO_SCRIPTS_VERSION) return

        val examplesDir = File(filesDir, "examples").apply { mkdirs() }
        for (name in listOf(
            "lcd_shield.py",
            "find_line_segments.py",
            "face_detection.py",
            "face_eye_detection.py",
            "iris_detection.py",
            "find_circles.py",
            "find_apriltags.py",
            "find_rects.py",
            "find_lines.py",
            "find_edges.py",
            "find_lbp.py",
            "face_tracking.py",
            "find_hog.py",
            "find_displacement.py",
            "find_datamatrices.py",
            "find_template.py",
            "location.py",
            "mqtt_tls.py",
            "tflite_selftest.py",
            "litert_selftest.py",
            "add_simple.tflite",
            "single_add_quant.tflite",
        )) {
            assets.open("examples/$name").use { input ->
                File(examplesDir, name).outputStream().use { output ->
                    input.copyTo(output)
                }
            }
        }
        settingsManager.demoScriptsVersion = CURRENT_DEMO_SCRIPTS_VERSION
    }

    // Seeds assets/rom/ (Haar cascades) into the VFS's /rom/, the path
    // OpenMV scripts load them from. The version is only stamped when
    // assets/rom/ is non-empty, so a build without generated cascades
    // does not block a later build that has them.
    private fun seedRomIfNeeded() {
        if (settingsManager.romVersion >= CURRENT_ROM_VERSION) return

        val names = assets.list("rom").orEmpty()
        if (names.isEmpty()) return
        val romDir = File(filesDir, "rom").apply { mkdirs() }
        for (name in names) {
            assets.open("rom/$name").use { input ->
                File(romDir, name).outputStream().use { output ->
                    input.copyTo(output)
                }
            }
        }
        settingsManager.romVersion = CURRENT_ROM_VERSION
    }
}
