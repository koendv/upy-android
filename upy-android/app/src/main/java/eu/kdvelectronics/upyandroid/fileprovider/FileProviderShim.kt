@file:JvmName("FileProviderShim")
package eu.kdvelectronics.upyandroid.fileprovider

import android.content.Context
import android.content.Intent
import androidx.core.content.FileProvider
import java.io.File

// Builds the real content:// URI + ACTION_SEND chooser for
// android.fileprovider.share(). Must run in the main process (has the
// UI context needed to start a chooser Activity). See
// MainActivity.kt's own boardManager.setShareRequestListener() wiring
// and EngineService.kt's requestShare() for how a request gets here
// from :engine.
//
// path is a VFS path exactly as the script wrote it (e.g. "/foo.jpg" or
// "foo.jpg"). File(Context, String) resolves a leading "/" against
// filesDir rather than treating it as absolute (see javadoc: an
// absolute child pathname is converted into a relative one), so this
// matches VfsPosix's own root-at-"/" mount scheme without needing any
// separate translation.
fun shareFile(context: Context, path: String, mimeType: String) {
    val file = File(context.filesDir, path)
    val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", file)
    val intent = Intent(Intent.ACTION_SEND).apply {
        type = mimeType
        putExtra(Intent.EXTRA_STREAM, uri)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    // Called on a Binder thread pool thread (the AIDL callback), not the
    // UI thread. startActivity() does not require the calling thread
    // to be the UI thread, only that context be a real Activity context,
    // which MainActivity's own registration below provides.
    context.startActivity(Intent.createChooser(intent, null))
}

// Files > Open with: ACTION_VIEW with read and write permission, so an
// editor can save its changes straight back into the VFS file. Editors
// register for opening files (VIEW), rarely for shares (SEND).
fun openWith(context: Context, path: String, mimeType: String) {
    val file = File(context.filesDir, path)
    val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", file)
    val intent = Intent(Intent.ACTION_VIEW).apply {
        setDataAndType(uri, mimeType)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(intent, null))
}
