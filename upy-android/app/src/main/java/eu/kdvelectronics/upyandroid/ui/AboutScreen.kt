package eu.kdvelectronics.upyandroid.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.text.Html
import android.text.method.LinkMovementMethod
import android.widget.TextView
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import java.util.Properties

private const val SOURCE_URL = "https://github.com/koendv/upy-android"

// Settings > About: version and build facts, like Android's "About phone",
// plus rows leading to Attributions (NOTICE.html) and Licenses (LICENSES.txt).
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AboutScreen(onBack: () -> Unit, onOpenAttributions: () -> Unit, onOpenLicenses: () -> Unit) {
    val context = LocalContext.current
    val versionName = remember {
        try {
            val info = context.packageManager.getPackageInfo(context.packageName, 0)
            "${info.versionName} (${androidx.core.content.pm.PackageInfoCompat.getLongVersionCode(info)})"
        } catch (e: android.content.pm.PackageManager.NameNotFoundException) {
            "unknown"
        }
    }
    // Written by build.gradle.kts's generateBuildInfo task.
    val buildInfo = remember {
        Properties().apply {
            try {
                context.assets.open("build_info.properties").use { load(it) }
            } catch (e: java.io.IOException) {
                // Rows below fall back to "unknown".
            }
        }
    }
    val commit = buildInfo.getProperty("git.commit", "unknown")
    val micropythonSha = buildInfo.getProperty("micropython.sha", "")

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("About") },
                navigationIcon = {
                    TooltipIconButton(icon = SymbolIcon.ARROW_BACK, label = "Back", onClick = onBack)
                },
            )
        },
    ) { padding: PaddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState()),
        ) {
            AboutRow("upy-android", versionName)
            AboutRow("Build date", buildInfo.getProperty("build.date", "unknown"))
            AboutRow(
                "Source code",
                if (commit == "unknown") SOURCE_URL else "$SOURCE_URL @ ${commit.take(7)}",
                onClick = {
                    openUrl(context, if (commit == "unknown") SOURCE_URL else "$SOURCE_URL/tree/$commit")
                },
            )
            AboutRow(
                "MicroPython",
                buildInfo.getProperty("micropython.version", "unknown") +
                    if (micropythonSha.isNotEmpty()) " (${micropythonSha.take(7)})" else "",
            )
            AboutLinkRow("Attributions", onOpenAttributions)
            AboutLinkRow("Licenses", onOpenLicenses)
        }
    }
}

@Composable
private fun AboutRow(title: String, value: String, onClick: (() -> Unit)? = null) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .padding(vertical = 12.dp),
    ) {
        Text(title)
        Text(value, style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun AboutLinkRow(title: String, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(title, modifier = Modifier.weight(1f))
        Text(">")
    }
}

private fun openUrl(context: Context, url: String) {
    try {
        context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
    } catch (e: android.content.ActivityNotFoundException) {
        // No browser installed; the URL is still shown in the row.
    }
}

// Settings > About > Attributions: NOTICE.html, which component is used
// under which license.
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AttributionsScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val noticeHtml = remember {
        try {
            context.assets.open("NOTICE.html").bufferedReader().use { it.readText() }
        } catch (e: java.io.IOException) {
            "<p>NOTICE.html could not be loaded: $e</p>"
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Attributions") },
                navigationIcon = {
                    TooltipIconButton(icon = SymbolIcon.ARROW_BACK, label = "Back", onClick = onBack)
                },
            )
        },
    ) { padding: PaddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
        ) {
            AndroidView(
                factory = { ctx ->
                    TextView(ctx).apply {
                        movementMethod = LinkMovementMethod.getInstance()
                    }
                },
                update = { textView ->
                    textView.text = Html.fromHtml(noticeHtml, Html.FROM_HTML_MODE_COMPACT)
                },
            )
        }
    }
}

// Settings > About > Licenses: full license texts (LICENSES.txt), then
// LiteRT's third-party notices from its AAR. About 2 MB of text, so shown
// line by line in a LazyColumn rather than as one Text.
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LicensesScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val lines = remember {
        listOf("LICENSES.txt", "litert_third_party_notices.txt").flatMap { name ->
            try {
                context.assets.open(name).bufferedReader().use { it.readLines() } + ""
            } catch (e: java.io.IOException) {
                listOf("$name could not be loaded: $e", "")
            }
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Licenses") },
                navigationIcon = {
                    TooltipIconButton(icon = SymbolIcon.ARROW_BACK, label = "Back", onClick = onBack)
                },
            )
        },
    ) { padding: PaddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 16.dp),
        ) {
            items(lines) { line ->
                Text(line, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
            }
        }
    }
}
