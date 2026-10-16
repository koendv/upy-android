package eu.kdvelectronics.upyandroid.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// Brand accent, matching the launcher icon's own cyan-on-charcoal
// design (see colors.xml/ic_launcher_foreground.xml). Deliberately
// not dynamic/Material-You color (dynamicLightColorScheme/
// dynamicDarkColorScheme), which would replace this with whatever the
// device wallpaper happens to produce, fighting the identity that icon
// redesign was for. System light/dark switching only.
private val BrandAccent = Color(0xFF35D0FF)

// Plain black/white rather than Material3's own muted off-black/off-
// white tonal defaults. Matches this app's own terminal/command-line
// feel (see the Command screen's ">>>" prompt) more directly than a
// softened neutral gray would.
// The container roles are set too, as neutral greys with a tint of the accent
// for the selected item: left at Material3's defaults they are lavender in
// light mode (the navigation bar) and purple-grey in dark mode.
private val LightColors = lightColorScheme(
    primary = BrandAccent,
    onPrimary = Color.Black,
    background = Color.White,
    onBackground = Color.Black,
    surface = Color.White,
    onSurface = Color.Black,
    surfaceContainerLowest = Color.White,
    surfaceContainerLow = Color(0xFFF8F8F8),
    surfaceContainer = Color(0xFFF4F4F4),
    surfaceContainerHigh = Color(0xFFEEEEEE),
    surfaceContainerHighest = Color(0xFFE8E8E8),
    secondaryContainer = Color(0xFFCDEFFC),
    onSecondaryContainer = Color.Black,
)
private val DarkColors = darkColorScheme(
    primary = BrandAccent,
    onPrimary = Color.Black,
    background = Color.Black,
    onBackground = Color.White,
    surface = Color.Black,
    onSurface = Color.White,
    surfaceContainerLowest = Color.Black,
    surfaceContainerLow = Color(0xFF0C0C0C),
    surfaceContainer = Color(0xFF141414),
    surfaceContainerHigh = Color(0xFF1C1C1C),
    surfaceContainerHighest = Color(0xFF242424),
    secondaryContainer = Color(0xFF0C3F52),
    onSecondaryContainer = Color.White,
)

@Composable
fun UpyTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = if (isSystemInDarkTheme()) DarkColors else LightColors,
        content = content,
    )
}
