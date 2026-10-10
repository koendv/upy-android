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

    // Gates AdbExecProvider's call(), see AdbExecProvider.kt. Default
    // false: a fresh install must not expose an adb-driven exec channel
    // until the user deliberately opts in via the UI toggle.
    var adbExecEnabled: Boolean
        get() = prefs.getBoolean(KEY_ADB_EXEC_ENABLED, false)
        set(value) = prefs.edit().putBoolean(KEY_ADB_EXEC_ENABLED, value).apply()

    // MicroPython's heap size, in MB. See engine_jni.cpp#allocate_heap.
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

    // Never pushed into :engine, never readable from a script.
    // See BoardManager.kt#pushSettings.
    var sshPassword: String
        get() = prefs.getString(KEY_SSH_PASSWORD, "") ?: ""
        set(value) = prefs.edit().putString(KEY_SSH_PASSWORD, value).apply()

    // Last bundled-demo-scripts version actually copied into the VFS's
    // own /examples/ directory. See MainActivity's own
    // seedDemoScriptsIfNeeded(). 0 (never seeded) on a fresh install.
    // Deliberately a version int, not a one-shot boolean: bumping
    // CURRENT_DEMO_SCRIPTS_VERSION lets a later app update that fixes a
    // bundled demo script reach existing installs too, not just new
    // ones. An already-seeded copy gets overwritten again once.
    var demoScriptsVersion: Int
        get() = prefs.getInt(KEY_DEMO_SCRIPTS_VERSION, 0)
        set(value) = prefs.edit().putInt(KEY_DEMO_SCRIPTS_VERSION, value).apply()

    // Same version-stamped reasoning, for the /rom/ files. See seedRomIfNeeded().
    var romVersion: Int
        get() = prefs.getInt(KEY_ROM_VERSION, 0)
        set(value) = prefs.edit().putInt(KEY_ROM_VERSION, value).apply()

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
        private const val KEY_DEMO_SCRIPTS_VERSION = "demo_scripts_version"
        private const val KEY_ROM_VERSION = "rom_version"
    }
}
