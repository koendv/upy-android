package eu.kdvelectronics.upyandroid.ui

import android.app.Activity
import android.content.pm.ActivityInfo
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import eu.kdvelectronics.upyandroid.managers.BoardManager

// Passive viewport, not a "run" screen. Vision scripts are written and
// launched through the existing Terminal/Explorer/Editor "Run" flow
// like any other script; this screen just shows whatever :engine is
// currently drawing, with no "Run" input of its own. Peer nav-suite tab
// now (Part 10) -- no onBack of its own, tab switching replaces it.
// see session-state: CameraScreen.kt#CameraScreen
//
// The SurfaceView must be a real View-backed Surface (AndroidView, not
// Compose-drawn): :engine writes to it natively via ANativeWindow. See
// display_module.cpp. surfaceCreated()/surfaceDestroyed() are the
// trigger points for the surface-lifecycle decision: a silent no-op
// while gone, re-attach on return, with the running script never
// interrupted by navigating off this screen.
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CameraScreen(
    boardManager: BoardManager,
) {
    val activity = LocalContext.current as Activity

    // Locks to one orientation while this screen is visible, restores
    // on exit. This removes device rotation as a recurring
    // surface-teardown trigger. Single-Activity app, so this means
    // saving and restoring requestedOrientation directly, not a
    // manifest flag, which would lock the whole app rather than just
    // this screen.
    DisposableEffect(Unit) {
        val previousOrientation = activity.requestedOrientation
        activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        onDispose {
            activity.requestedOrientation = previousOrientation
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Camera") },
                actions = {
                    TooltipIconButton(
                        icon = SymbolIcon.STOP,
                        label = "Interrupt",
                        onClick = { boardManager.interrupt() },
                    )
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            verticalArrangement = Arrangement.Top
        ) {
            // A SurfaceView's own Surface composites as opaque black
            // until something is actually written to it (:engine's own
            // ANativeWindow_lock/unlockAndPost, only while a script is
            // running) -- deliberately NOT given a background color to
            // match the app theme (tried in 718028d, reverted): a plain
            // SurfaceView's real Surface is composited BEHIND the normal
            // view hierarchy by default, so any background painted via
            // the View system (setBackgroundColor, every recomposition
            // via AndroidView's own update{}) sits ON TOP of the actual
            // native-written frame content, hiding it completely -- a
            // real regression only caught by actually trying to display
            // live camera frames, not by the earlier screenshot-only
            // "does the idle color match" check.
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { context ->
                    SurfaceView(context).apply {
                        holder.addCallback(object : SurfaceHolder.Callback {
                            override fun surfaceCreated(holder: SurfaceHolder) {
                                boardManager.setDisplaySurface(holder.surface)
                            }

                            override fun surfaceChanged(
                                holder: SurfaceHolder,
                                format: Int,
                                width: Int,
                                height: Int,
                            ) {
                                // No-op: display_module.cpp reads the
                                // Surface's current real size on every
                                // write() via ANativeWindow_lock's own
                                // ANativeWindow_Buffer, not a size cached
                                // at attach time.
                            }

                            override fun surfaceDestroyed(holder: SurfaceHolder) {
                                boardManager.setDisplaySurface(null)
                            }
                        })
                    }
                },
            )
        }
    }
}
