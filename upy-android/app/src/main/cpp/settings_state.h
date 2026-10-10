#pragma once

// Snapshot of non-secret settings. heap_size_mb is written exactly
// once, by nativeInit() (engine_jni.cpp), and never changes again for
// this :engine process's lifetime. Changing it needs a real app
// restart, not just Reset. The other two fields are written by
// nativeSetSettings() (called directly on a Binder thread, never
// queued through EngineWorker's task queue) whenever the user changes
// them, live. Read by android.settings (settings_module.cpp) on the
// worker thread. Never includes ssh_password, which stays in SettingsManager.kt, never copied here, never readable from a
// script. No <jni.h> here: this header is included from
// settings_module.cpp, which is qstr-scanned and must stay JNI-free,
// same reasoning as litert_jni_bridge.h.
struct SettingsSnapshot {
    int heap_size_mb;
    bool ssh_enabled;
    bool adb_exec_enabled;
};

SettingsSnapshot settings_snapshot_get();
