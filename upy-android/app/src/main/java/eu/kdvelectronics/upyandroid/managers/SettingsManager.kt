package eu.kdvelectronics.upyandroid.managers

import android.content.Context

/**
 * This app's one persisted settings store: a single SharedPreferences
 * file. Settings are named properties, not a generic string-key get/set,
 * so each one is a real, typed, greppable API surface as more get added,
 * not an arbitrary-key free-for-all.
 */
class SettingsManager(context: Context) {
    private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    // Tracks whether the app has ever shown the OS's camera-permission
    // dialog, distinct from whether it was granted. See MainActivity's
    // maybeRequestCameraPermission() for why this cannot be derived from
    // shouldShowRequestPermissionRationale() alone.
    var askedCameraPermission: Boolean
        get() = prefs.getBoolean(KEY_ASKED_CAMERA_PERMISSION, false)
        set(value) = prefs.edit().putBoolean(KEY_ASKED_CAMERA_PERMISSION, value).apply()

    companion object {
        private const val PREFS_NAME = "upy_android_settings"
        private const val KEY_ASKED_CAMERA_PERMISSION = "asked_camera_permission"
    }
}
