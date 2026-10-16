@file:JvmName("ZipShim")
// JNI-facing glue for the zipfile module (zipfile_module.cpp,
// zipfile_jni_bridge.cpp). Flat, top-level functions with JNI-simple
// types, like MqttShim.kt. Paths are relative to the app's files dir,
// the same root VfsPosix mounts at "/". Not interruptible.
package eu.kdvelectronics.upyandroid.zip

import eu.kdvelectronics.upyandroid.VfsPath
import eu.kdvelectronics.upyandroid.VfsPath.resolve
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

// "<size> <name>" per entry; a folder entry ends in "/" and has size 0.
fun info(zip: String): Array<String> =
    ZipFile(resolve(zip)).use { z ->
        z.entries().asSequence().map { "${maxOf(it.size, 0L)} ${it.name}" }.toList().toTypedArray()
    }

// Extracts every entry into the folder dest. An entry stops the extraction
// with an IOException when its name has a leading "/" or a ".." part, or when
// its CRC does not match. Entries before it stay on disk.
fun extractAll(zip: String, dest: String) {
    val destDir = resolve(dest)
    if (destDir.exists() && !destDir.isDirectory) throw IOException("not a folder: $dest")
    destDir.mkdirs()
    ZipFile(resolve(zip)).use { z ->
        for (entry in z.entries()) {
            val name = entry.name
            if (name.startsWith("/") || name.split('/').contains("..")) {
                throw IOException("unsafe entry name: $name")
            }
            val out = File(destDir, name)
            if (entry.isDirectory) {
                out.mkdirs()
                continue
            }
            out.parentFile?.mkdirs()
            val crc = CRC32()
            try {
                z.getInputStream(entry).use { input ->
                    out.outputStream().use { output ->
                        val buffer = ByteArray(64 * 1024)
                        while (true) {
                            val n = input.read(buffer)
                            if (n < 0) break
                            crc.update(buffer, 0, n)
                            output.write(buffer, 0, n)
                        }
                    }
                }
                if (entry.crc != -1L && entry.crc != crc.value) throw IOException("bad CRC: $name")
            } catch (e: IOException) {
                out.delete()
                throw e
            }
        }
    }
}

// Replaces the zip zipPath with the file or folder tree path. Entry names are
// relative to the parent of path ("data/a.txt" for path "data"). Files only:
// an empty folder is not stored. The zip itself is skipped when it lies in
// the folder.
fun create(zip: String, path: String) {
    val source = resolve(path)
    val target = resolve(zip)
    if (!source.exists()) throw FileNotFoundException(path)
    val base = if (source == VfsPath.rootDir) source else source.parentFile!!
    try {
        ZipOutputStream(target.outputStream().buffered()).use { zos ->
            fun add(file: File) {
                if (file.canonicalFile == target) return
                if (file.isDirectory) {
                    file.listFiles()?.sortedBy { it.name }?.forEach { add(it) }
                } else {
                    zos.putNextEntry(ZipEntry(file.relativeTo(base).path.replace(File.separatorChar, '/')))
                    file.inputStream().use { it.copyTo(zos) }
                    zos.closeEntry()
                }
            }
            add(source)
        }
    } catch (e: IOException) {
        target.delete()
        throw e
    }
}
