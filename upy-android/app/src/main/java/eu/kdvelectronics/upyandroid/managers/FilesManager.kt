package eu.kdvelectronics.upyandroid.managers

import eu.kdvelectronics.upyandroid.model.MicroFile
import java.io.File

/**
 * Local file operations rooted at [root] -- the SAME directory
 * EngineService passes to nativeInit/nativeReset as the VFS root, so
 * whatever this browses/edits is exactly what MicroPython scripts see
 * via open()/os.listdir(). Plain java.io.File calls, not a remote-board
 * REPL round-trip like micro-repl's own FilesManager (MIT, see
 * NOTICE.md) -- this app has no remote board, the "board" IS this local
 * sandboxed directory, so there's no transport to simulate.
 *
 * All methods do blocking I/O -- call from a background thread/coroutine
 * (Dispatchers.IO), matching TerminalManager's own contract, never the
 * UI thread.
 */
class FilesManager(private val root: File) {

    /** Lists files/directories directly under [path] (relative to root; "" = root). */
    fun listDir(path: String): List<MicroFile> {
        val entries = resolve(path).listFiles() ?: return emptyList()
        return entries.map { f ->
            MicroFile(
                name = f.name,
                path = path,
                isDirectory = f.isDirectory,
                size = if (f.isFile) f.length() else 0L
            )
        }.sortedWith(compareBy({ !it.isDirectory }, { it.name.lowercase() }))
    }

    fun read(file: MicroFile): String = resolve(file.fullPath).readText()

    fun write(file: MicroFile, content: String) {
        val target = resolve(file.fullPath)
        target.parentFile?.mkdirs()
        target.writeText(content)
    }

    fun writeBinary(path: String, bytes: ByteArray) {
        val target = resolve(path)
        target.parentFile?.mkdirs()
        target.writeBytes(bytes)
    }

    fun newFile(file: MicroFile) {
        val target = resolve(file.fullPath)
        target.parentFile?.mkdirs()
        if (!target.exists()) target.createNewFile()
    }

    fun newDirectory(file: MicroFile) {
        resolve(file.fullPath).mkdirs()
    }

    fun remove(file: MicroFile) {
        resolve(file.fullPath).deleteRecursively()
    }

    fun rename(file: MicroFile, newName: String): MicroFile {
        val src = resolve(file.fullPath)
        val dst = File(src.parentFile, newName)
        src.renameTo(dst)
        return file.copy(name = newName)
    }

    private fun resolve(path: String): File =
        if (path.isEmpty()) root else File(root, path)
}
