package eu.kdvelectronics.upyandroid.ui

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
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import eu.kdvelectronics.upyandroid.ScriptExecCore

// Passive viewport, not a "run" screen.
// see session-state: CameraScreen.kt#CameraScreen
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CameraScreen() {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Camera") },
                actions = {
                    TooltipIconButton(
                        icon = SymbolIcon.STOP,
                        label = "Interrupt",
                        onClick = { ScriptExecCore.interruptNow() },
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
            // see session-state: CameraScreen.kt#CameraScreen
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { context ->
                    SurfaceView(context).apply {
                        holder.addCallback(object : SurfaceHolder.Callback {
                            override fun surfaceCreated(holder: SurfaceHolder) {
                                ScriptExecCore.setDisplaySurface(holder.surface)
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
                                ScriptExecCore.setDisplaySurface(null)
                            }
                        })
                    }
                },
            )
        }
    }
}
