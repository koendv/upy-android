@file:JvmName("MediaStoreShim")
// Thin JNI-facing glue for android.mediastore (mediastore_module.cpp/
// mediastore_jni_bridge.cpp), same "flat top-level functions, JNI-
// simple parameter/return types" shape as LiteRtShim.kt. Write-only:
// scripts save script-produced media into shared, gallery-visible
// MediaStore collections instead of the private VFS. No read access
// (would need READ_MEDIA_IMAGES, a different use case, not requested).
package eu.kdvelectronics.upyandroid.mediastore

import android.content.ContentValues
import android.content.Context
import android.provider.MediaStore
import java.io.IOException

// Returns the new item's real content:// URI as a string. The
// caller (mediastore_module.cpp) hands this back to the script
// verbatim, it's the only handle a script has on what it just saved.
fun saveImage(context: Context, data: ByteArray, displayName: String, mimeType: String): String {
    val values = ContentValues().apply {
        put(MediaStore.Images.Media.DISPLAY_NAME, displayName)
        put(MediaStore.Images.Media.MIME_TYPE, mimeType)
    }
    val resolver = context.contentResolver
    val uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)
        ?: throw IOException("MediaStore insert() returned null")
    resolver.openOutputStream(uri)?.use { it.write(data) }
        ?: throw IOException("MediaStore openOutputStream() returned null")
    return uri.toString()
}
