// JNI bridge for the upy-android MicroPython engine, running inside the
// :engine process (see EngineService.kt / AndroidManifest.xml).
// see session-state: engine_jni.cpp#threading_contract

#include <jni.h>
#include <cstdlib>
#include <mutex>
#include <string>
#include <android/native_window_jni.h>

extern "C" {
#include "port/micropython_embed.h"
#include "py/runtime.h"
}

#include "camera_module.h"
#include "display_module.h"
#include "fileprovider_module.h"
#include "imu_module.h"
#include "litert_module.h"
#include "mediastore_module.h"
#include "rt_module.h"
#include "settings_state.h"
#include "tf_module.h"

namespace {
constexpr int kDefaultHeapSizeMb = 32;
char *g_heap = nullptr;
size_t g_heap_size = 0;
bool g_initialized = false;

std::mutex g_settings_mutex;
SettingsSnapshot g_settings_snapshot = {kDefaultHeapSizeMb, false, false, false, false, false};

// see session-state: engine_jni.cpp#allocate_heap
void allocate_heap(int heap_size_mb) {
    size_t requested = static_cast<size_t>(heap_size_mb) * 1024 * 1024;
    char *buf = static_cast<char *>(malloc(requested));
    if (!buf) {
        requested = static_cast<size_t>(kDefaultHeapSizeMb) * 1024 * 1024;
        buf = static_cast<char *>(malloc(requested));
    }
    g_heap = buf;
    g_heap_size = requested;
}

// Cached once, on the single persistent "mp-engine-worker" thread (see
// EngineWorker.kt), before any script runs. That thread is a real
// java.lang.Thread from birth and stays JVM-attached for the :engine
// process's entire lifetime, so litert_jni_bridge.cpp's GetEnv()-per-call
// pattern (built on this JavaVM*) never needs Attach/DetachCurrentThread.
// see session-state: litert_jni_bridge.cpp#threading
JavaVM *g_jvm = nullptr;

// Bridges mp_embed's plain-C chunk callback to a JNI upcall on the sink
// object passed into nativeExec. Lives only for the duration of one
// nativeExec() call.
struct ChunkCbContext {
    JNIEnv *env;
    jobject sink;
    jmethodID on_chunk_method;
};

void chunk_cb_trampoline(const char *str, size_t len, void *context) {
    auto *ctx = static_cast<ChunkCbContext *>(context);
    // str is not null-terminated at len. Build an explicit-length
    // std::string first, same as nativeExec does for the final
    // accumulated output.
    std::string chunk(str, len);
    jstring jchunk = ctx->env->NewStringUTF(chunk.c_str());
    ctx->env->CallVoidMethod(ctx->sink, ctx->on_chunk_method, jchunk);
    ctx->env->DeleteLocalRef(jchunk);
}
}

SettingsSnapshot settings_snapshot_get() {
    std::lock_guard<std::mutex> lock(g_settings_mutex);
    return g_settings_snapshot;
}

// heapSizeMb: read once here, from EngineWorker's own constructor arg
// (itself read synchronously from SettingsManager in
// EngineService.onCreate(), no AIDL involved) -- see
// EngineWorker.kt#start for why this must NOT depend on anything
// pushed over IEngine.aidl#setSettings. Fixed for this :engine
// process's entire lifetime; changing it needs a real app restart, not
// just Reset -- see nativeReset below.
// applicationContext: a real android.content.Context, needed by
// mediastore_module.cpp's own MediaStore access (ContentResolver is
// only reachable through a Context -- unlike litert_bridge_init, which
// needs nothing beyond a JNIEnv). See EngineWorker.kt#start /
// EngineService.kt for where this comes from (EngineService's own
// applicationContext, read once here, at :engine's own init time --
// same "read once, not pushed live" tier as heapSizeMb/rootPath).
extern "C" JNIEXPORT jboolean JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeInit(JNIEnv *env, jobject, jint stackSizeBytes, jint heapSizeMb, jstring rootPath, jobject applicationContext) {
    if (g_initialized) {
        return JNI_TRUE;
    }
    if (!g_jvm) {
        env->GetJavaVM(&g_jvm);
        litert_bridge_init(env);
        mediastore_bridge_init(env, applicationContext);
        fileprovider_bridge_init(env);
    }
    const char *root_path_chars = env->GetStringUTFChars(rootPath, nullptr);
    allocate_heap(static_cast<int>(heapSizeMb));
    {
        std::lock_guard<std::mutex> lock(g_settings_mutex);
        g_settings_snapshot.heap_size_mb = static_cast<int>(heapSizeMb);
    }
    int stack_top;
    mp_embed_init(g_heap, g_heap_size, &stack_top, static_cast<size_t>(stackSizeBytes), root_path_chars);
    env->ReleaseStringUTFChars(rootPath, root_path_chars);
    g_initialized = true;
    return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeExec(JNIEnv *env, jobject, jstring code, jobject sink) {
    const char *code_chars = env->GetStringUTFChars(code, nullptr);
    mp_embed_output_clear();

    ChunkCbContext ctx{};
    if (sink != nullptr) {
        jclass sink_class = env->GetObjectClass(sink);
        jmethodID on_chunk_method = env->GetMethodID(sink_class, "onChunk", "(Ljava/lang/String;)V");
        env->DeleteLocalRef(sink_class);
        ctx = ChunkCbContext{env, sink, on_chunk_method};
        mp_embed_set_output_chunk_cb(&chunk_cb_trampoline, &ctx);
    }

    mp_embed_exec_str(code_chars);

    mp_embed_set_output_chunk_cb(nullptr, nullptr);
    std::string output = mp_embed_output_get();
    env->ReleaseStringUTFChars(code, code_chars);
    return env->NewStringUTF(output.c_str());
}

// Safe to call from any thread. Sets a pending-exception flag the
// worker thread's VM loop polls between bytecode instructions. Does not
// touch heap/stack/gc state, so no synchronization is needed here.
extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeInterrupt(JNIEnv *, jobject) {
    mp_sched_keyboard_interrupt();
    // Also wakes an in-flight csi.snapshot() immediately, rather than
    // leaving it to time out on its own 500ms deadline. Safe from any
    // thread, same contract as this function's own.
    camera_interrupt_active_wait();
}

// Must be called on the worker thread, like nativeExec. Reuses the
// SAME g_heap buffer nativeInit() allocated -- heap size is fixed for
// this :engine process's whole lifetime (set once at nativeInit(),
// changed only by a real app restart, not by Reset -- see
// nativeInit's own comment); no free/realloc here.
// see session-state: engine_jni.cpp#nativeReset
extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeReset(JNIEnv *env, jobject, jint stackSizeBytes, jstring rootPath) {
    camera_close_all();
    imu_close_all();
    tf_close_all();
    rt_close_all();
    litert_close_all();

    if (g_initialized) {
        mp_embed_deinit();
    }
    const char *root_path_chars = env->GetStringUTFChars(rootPath, nullptr);
    int stack_top;
    mp_embed_init(g_heap, g_heap_size, &stack_top, static_cast<size_t>(stackSizeBytes), root_path_chars);
    env->ReleaseStringUTFChars(rootPath, root_path_chars);
    g_initialized = true;
}

extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeDeinit(JNIEnv *, jobject) {
    // Defense-in-depth, not a fix for a known gap. Idle-unbind/
    // crash-kill both already get equivalent camera-service-side
    // cleanup for free via process death; this removes reliance on
    // that assumption rather than leaving it unverified.
    camera_close_all();
    imu_close_all();
    tf_close_all();
    rt_close_all();
    litert_close_all();
    if (g_initialized) {
        mp_embed_deinit();
        g_initialized = false;
    }
    free(g_heap);
    g_heap = nullptr;
    g_heap_size = 0;
}

// nativeSetSettings: safe from any thread, same as nativeSetDisplaySurface.
// heap_size_mb is deliberately NOT a parameter here -- it is read once,
// at nativeInit() time only, and is immutable for this :engine
// process's lifetime (see nativeInit's own comment); this call must
// not overwrite g_settings_snapshot's own heap_size_mb field with a
// not-yet-applied edit, or android.settings() would misreport it.
// see session-state: engine_jni.cpp#nativeSetSettings
extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeSetSettings(
    JNIEnv *, jobject,
    jboolean sshEnabled, jboolean httpServerEnabled,
    jboolean httpPrivateFilesEnabled, jboolean litertPlaystoreEnabled,
    jboolean adbExecEnabled) {
    std::lock_guard<std::mutex> lock(g_settings_mutex);
    g_settings_snapshot.ssh_enabled = sshEnabled;
    g_settings_snapshot.http_server_enabled = httpServerEnabled;
    g_settings_snapshot.http_private_files_enabled = httpPrivateFilesEnabled;
    g_settings_snapshot.litert_playstore_enabled = litertPlaystoreEnabled;
    g_settings_snapshot.adb_exec_enabled = adbExecEnabled;
}

// see session-state: engine_jni.cpp#nativeSetDisplaySurface
extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeSetDisplaySurface(JNIEnv *env, jobject, jobject surface) {
    ANativeWindow *window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    display_set_window(window);
}
