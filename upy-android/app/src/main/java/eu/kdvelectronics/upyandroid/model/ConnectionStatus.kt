package eu.kdvelectronics.upyandroid.model

// Local-transport connection status -- much simpler than micro-repl's
// USB-serial original (no device discovery/approval flow needed, since
// there's exactly one thing to connect to: our own :engine process).
sealed class ConnectionStatus {
    data object Connecting : ConnectionStatus()
    data object Connected : ConnectionStatus()
    data class Disconnected(val reason: String) : ConnectionStatus()

    val isConnected: Boolean get() = this is Connected
}
