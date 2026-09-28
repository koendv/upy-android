package eu.kdvelectronics.upyandroid.ui

import android.text.Html
import android.text.method.LinkMovementMethod
import android.widget.TextView
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView

// Non-peer detail screen, reached only via Settings' own "About" row.
// Same tier as EditorScreen (reached only via Explorer), not a
// NavigationSuiteScaffold destination of its own.
//
// NOTICE.html is bundled as a plain asset (build.gradle.kts's own
// copyNotice task copies the repo-root file in at build time) and
// rendered via the platform's own android.text.Html.fromHtml() into a
// plain TextView. No markdown/HTML rendering library needed, and
// LinkMovementMethod makes the <a href> source links actually tappable.
// minSdk is 27, well above the API 24 the single-arg Html.fromHtml()
// was deprecated at, so the two-arg FROM_HTML_MODE_COMPACT form is used
// directly (no androidx.core/HtmlCompat dependency needed either).
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AboutScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val noticeHtml = remember {
        try {
            context.assets.open("NOTICE.html").bufferedReader().use { it.readText() }
        } catch (e: java.io.IOException) {
            "<p>NOTICE.html could not be loaded: $e</p>"
        }
    }
    val versionName = remember {
        try {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName
        } catch (e: android.content.pm.PackageManager.NameNotFoundException) {
            null
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("About") },
                navigationIcon = {
                    TooltipIconButton(
                        icon = SymbolIcon.ARROW_BACK,
                        label = "Back",
                        onClick = onBack,
                    )
                }
            )
        }
    ) { padding: PaddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp)
                .verticalScroll(rememberScrollState())
        ) {
            Text("upy" + if (versionName != null) " $versionName" else "")
            AndroidView(
                factory = { ctx ->
                    TextView(ctx).apply {
                        movementMethod = LinkMovementMethod.getInstance()
                    }
                },
                update = { textView ->
                    textView.text = Html.fromHtml(noticeHtml, Html.FROM_HTML_MODE_COMPACT)
                },
                modifier = Modifier.padding(top = 16.dp)
            )
        }
    }
}
