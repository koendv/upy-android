package eu.kdvelectronics.upyandroid.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.dp
import eu.kdvelectronics.upyandroid.ScriptExecCore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

// Shown while a running script waits for input (sys.stdin, input()), over
// any tab. Anchored above the keyboard, with no dimmed background, so the
// question the script just printed stays readable in the log. Enter sends
// the line with a "\n". Back or the close button interrupts the script.
@Composable
fun InputPopup(modifier: Modifier = Modifier) {
    val waiting by ScriptExecCore.inputWaiting.collectAsState()
    if (!waiting) return

    var text by remember { mutableStateOf("") }
    val scope = rememberCoroutineScope()
    val focusRequester = remember { FocusRequester() }
    val keyboard = LocalSoftwareKeyboardController.current
    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
        keyboard?.show()
    }
    BackHandler { ScriptExecCore.interruptNow() }

    fun send() {
        val line = text + "\n"
        text = ""
        scope.launch(Dispatchers.IO) { ScriptExecCore.sendInput(line.toByteArray(Charsets.UTF_8)) }
    }

    Surface(
        modifier = modifier.fillMaxWidth().imePadding().navigationBarsPadding().padding(8.dp),
        shape = RoundedCornerShape(12.dp),
        tonalElevation = 6.dp,
        shadowElevation = 6.dp,
    ) {
        Row(
            modifier = Modifier.padding(start = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Symbol(SymbolIcon.KEYBOARD, contentDescription = "Input")
            OutlinedTextField(
                value = text,
                onValueChange = { text = it },
                modifier = Modifier.weight(1f).padding(horizontal = 8.dp, vertical = 8.dp).focusRequester(focusRequester),
                textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                singleLine = true,
                keyboardOptions = KeyboardOptions(
                    imeAction = ImeAction.Send,
                    autoCorrectEnabled = false,
                    capitalization = KeyboardCapitalization.None,
                ),
                keyboardActions = KeyboardActions(onSend = { send() }),
            )
            TooltipIconButton(SymbolIcon.CLOSE, "Interrupt", onClick = { ScriptExecCore.interruptNow() })
        }
    }
}
