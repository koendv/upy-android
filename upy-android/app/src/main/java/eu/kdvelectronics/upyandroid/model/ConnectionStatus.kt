package eu.kdvelectronics.upyandroid.model

// Local-transport connection status. No device discovery or approval
// flow needed, since there is exactly one thing to connect to: this
// app's own :engine process.
sealed class ConnectionStatus {
    data object Connecting : ConnectionStatus()
    data object Connected : ConnectionStatus()
    data class Disconnected(val reason: String) : ConnectionStatus()

    val isConnected: Boolean get() = this is Connected
}
