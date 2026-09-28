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
private val LightColors = lightColorScheme(
    primary = BrandAccent,
    onPrimary = Color.Black,
    background = Color.White,
    onBackground = Color.Black,
    surface = Color.White,
    onSurface = Color.Black,
)
private val DarkColors = darkColorScheme(
    primary = BrandAccent,
    onPrimary = Color.Black,
    background = Color.Black,
    onBackground = Color.White,
    surface = Color.Black,
    onSurface = Color.White,
)

@Composable
fun UpyTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = if (isSystemInDarkTheme()) DarkColors else LightColors,
        content = content,
    )
}
