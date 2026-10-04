package eu.kdvelectronics.upyandroid.model

// One script run: printed output (including any traceback), and the type
// name of the uncaught exception that ended it, "" if none.
data class ExecResult(val output: String, val exception: String)
