package eu.kdvelectronics.upyandroid.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.PlainTooltip
import androidx.compose.material3.Text
import androidx.compose.material3.TooltipAnchorPosition
import androidx.compose.material3.TooltipBox
import androidx.compose.material3.TooltipDefaults
import androidx.compose.material3.rememberTooltipState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import eu.kdvelectronics.upyandroid.R

// see session-state: Symbol.kt#SymbolIcon
object SymbolIcon {
    const val TERMINAL = 0xeb8e
    const val FOLDER = 0xe2c7
    const val CAMERA = 0xe3af
    const val SETTINGS = 0xe8b8
    const val LINK = 0xe250
    const val LINK_OFF = 0xe16f
    const val STOP = 0xe047
    const val PLAY_ARROW = 0xe037
    const val RESTART_ALT = 0xf053
    const val DELETE_SWEEP = 0xe16c
    const val KEYBOARD_ARROW_UP = 0xe316
    const val KEYBOARD_ARROW_DOWN = 0xe313
    const val DRIVE_FOLDER_UPLOAD = 0xe9a3
    const val NOTE_ADD = 0xe89c
    const val CREATE_NEW_FOLDER = 0xe2cc
    const val REFRESH = 0xe5d5
    const val ARROW_BACK = 0xe5c4
    const val MEMORY = 0xe322
    const val TERMINAL_2 = 0xfff8e
    const val PASSWORD_2 = 0xf4a9
    const val PUBLIC = 0xe80b
    const val LOCK = 0xe899
    const val SHOP = 0xe8c9
    const val ADB = 0xe60e
    const val EDIT = 0xf097
    const val DELETE = 0xe92e
    const val DRIVE_FILE_RENAME_OUTLINE = 0xe9a2
    const val FOLDER_OPEN = 0xe2c8
    const val VERTICAL_ALIGN_BOTTOM = 0xe258
    const val CONTENT_COPY = 0xe14d
}

private val symbolFontFamily = FontFamily(Font(R.font.upy_symbols))

// see session-state: Symbol.kt#Symbol
@Composable
fun Symbol(
    codepoint: Int,
    contentDescription: String?,
    modifier: Modifier = Modifier,
    tint: Color = Color.Unspecified,
    size: Dp = 24.dp,
) {
    val density = LocalDensity.current
    val fixedFontSize = (size.value / density.fontScale).sp
    val semanticsModifier = if (contentDescription != null) {
        Modifier.semantics { this.contentDescription = contentDescription }
    } else {
        Modifier.clearAndSetSemantics { }
    }
    Box(
        modifier = modifier.size(size).then(semanticsModifier),
        contentAlignment = Alignment.Center,
    ) {
        // see session-state: Symbol.kt#Symbol
        Text(
            text = String(Character.toChars(codepoint)),
            fontFamily = symbolFontFamily,
            fontSize = fixedFontSize,
            lineHeight = fixedFontSize,
            style = LocalTextStyle.current.copy(
                lineHeightStyle = LineHeightStyle(
                    alignment = LineHeightStyle.Alignment.Center,
                    trim = LineHeightStyle.Trim.None,
                ),
                platformStyle = PlatformTextStyle(includeFontPadding = false),
            ),
            maxLines = 1,
            softWrap = false,
            color = tint,
        )
    }
}

// see session-state: Symbol.kt#TooltipIconButton
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TooltipIconButton(
    icon: Int,
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) = TooltipIconButton(label = label, onClick = onClick, modifier = modifier, enabled = enabled) {
    Symbol(icon, contentDescription = label)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TooltipIconButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    icon: @Composable () -> Unit,
) {
    TooltipBox(
        positionProvider = TooltipDefaults.rememberTooltipPositionProvider(TooltipAnchorPosition.Above),
        tooltip = { PlainTooltip { Text(label) } },
        state = rememberTooltipState(),
        modifier = modifier,
    ) {
        IconButton(onClick = onClick, enabled = enabled) {
            icon()
        }
    }
}
