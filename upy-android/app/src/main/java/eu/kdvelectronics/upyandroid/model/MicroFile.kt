package eu.kdvelectronics.upyandroid.model

/**
 * A file or directory entry under the app's sandboxed storage root --
 * the same root MicroPython's VFS mounts as "/" (see EngineService /
 * mpconfigport.h). Adapted from micro-repl's MicroFile (MIT, see
 * NOTICE.md), simplified: no remote-board stat type bits, no REPL-string
 * decoding -- this app has no remote board, so a plain isDirectory flag
 * is enough.
 */
data class MicroFile(
    val name: String,
    val path: String, // parent directory, relative to root; "" means root itself
    val isDirectory: Boolean,
    val size: Long = 0L
) {
    val fullPath: String
        get() = if (path.isEmpty()) name else "$path/$name"

    val isFile: Boolean get() = !isDirectory
    val canRun: Boolean get() = isFile && name.endsWith(".py")
}
