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

    // Gates AdbExecProvider's call() -- see AdbExecProvider.kt. Default
    // false: a fresh install must not expose an adb-driven exec channel
    // until the user deliberately opts in via the UI toggle.
    var adbExecEnabled: Boolean
        get() = prefs.getBoolean(KEY_ADB_EXEC_ENABLED, false)
        set(value) = prefs.edit().putBoolean(KEY_ADB_EXEC_ENABLED, value).apply()

    // MicroPython's heap size, in MB -- see engine_jni.cpp#allocate_heap.
    // Clamped here, not in native code: the real validation boundary is
    // this setter, not downstream of it.
    var heapSizeMb: Int
        get() = prefs.getInt(KEY_HEAP_SIZE_MB, DEFAULT_HEAP_SIZE_MB)
        set(value) = prefs.edit()
            .putInt(KEY_HEAP_SIZE_MB, value.coerceIn(MIN_HEAP_SIZE_MB, MAX_HEAP_SIZE_MB))
            .apply()

    var sshEnabled: Boolean
        get() = prefs.getBoolean(KEY_SSH_ENABLED, false)
        set(value) = prefs.edit().putBoolean(KEY_SSH_ENABLED, value).apply()

    // Never pushed into :engine, never readable from a script -- see
    // BoardManager.kt#pushSettings.
    var sshPassword: String
        get() = prefs.getString(KEY_SSH_PASSWORD, "") ?: ""
        set(value) = prefs.edit().putString(KEY_SSH_PASSWORD, value).apply()

    var httpServerEnabled: Boolean
        get() = prefs.getBoolean(KEY_HTTP_SERVER_ENABLED, false)
        set(value) = prefs.edit().putBoolean(KEY_HTTP_SERVER_ENABLED, value).apply()

    // Never pushed into :engine, never readable from a script -- see
    // BoardManager.kt#pushSettings.
    var httpPassword: String
        get() = prefs.getString(KEY_HTTP_PASSWORD, "") ?: ""
        set(value) = prefs.edit().putString(KEY_HTTP_PASSWORD, value).apply()

    // Gated behind httpServerEnabled in the UI (SettingsScreen.kt
    // disables this row's toggle unless the base server is on) -- the
    // stored value itself doesn't enforce that, since a base-server-off
    // + private-files-on combination is meaningless, not unsafe.
    var httpPrivateFilesEnabled: Boolean
        get() = prefs.getBoolean(KEY_HTTP_PRIVATE_FILES_ENABLED, false)
        set(value) = prefs.edit().putBoolean(KEY_HTTP_PRIVATE_FILES_ENABLED, value).apply()

    // Persisted and exposed via android.settings only -- does not yet
    // gate a real WorkManager init, since nothing in this build calls
    // AiPackModelProvider/AiPackManager to gate. See SESSION_STATE.yaml.
    var litertPlaystoreEnabled: Boolean
        get() = prefs.getBoolean(KEY_LITERT_PLAYSTORE_ENABLED, false)
        set(value) = prefs.edit().putBoolean(KEY_LITERT_PLAYSTORE_ENABLED, value).apply()

    companion object {
        private const val PREFS_NAME = "upy_android_settings"
        private const val KEY_ASKED_CAMERA_PERMISSION = "asked_camera_permission"
        private const val KEY_ADB_EXEC_ENABLED = "adb_exec_enabled"
        private const val KEY_HEAP_SIZE_MB = "heap_size_mb"
        private const val DEFAULT_HEAP_SIZE_MB = 32
        private const val MIN_HEAP_SIZE_MB = 4
        private const val MAX_HEAP_SIZE_MB = 512
        private const val KEY_SSH_ENABLED = "ssh_enabled"
        private const val KEY_SSH_PASSWORD = "ssh_password"
        private const val KEY_HTTP_SERVER_ENABLED = "http_server_enabled"
        private const val KEY_HTTP_PASSWORD = "http_password"
        private const val KEY_HTTP_PRIVATE_FILES_ENABLED = "http_private_files_enabled"
        private const val KEY_LITERT_PLAYSTORE_ENABLED = "litert_playstore_enabled"
    }
}
