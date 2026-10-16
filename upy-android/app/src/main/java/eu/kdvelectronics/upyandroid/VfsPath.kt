package eu.kdvelectronics.upyandroid

import java.io.File
import java.io.IOException

// Script paths to real files: the same root VfsPosix mounts at "/", the
// app's files dir. Used by the zipfile and http shims, which run in :engine.
object VfsPath {
    private var root = File("/")

    fun setRoot(path: String) {
        root = File(path).canonicalFile
    }

    // A leading "/" is relative to the root, as in open(). A path that
    // leaves the root raises IOException.
    fun resolve(path: String): File {
        val file = File(root, path).canonicalFile
        if (file != root && !file.path.startsWith(root.path + File.separator)) {
            throw IOException("path outside the files folder: $path")
        }
        return file
    }

    val rootDir: File get() = root
}
