// JNI bridge for the upy-android MicroPython engine, running inside the
// :engine process (see EngineService.kt / AndroidManifest.xml).
//
// Threading contract: nativeInit()/nativeExec()/nativeReset()/nativeDeinit()
// must all be called from the SAME OS thread -- the single reused worker
// thread created by EngineWorker.kt, whose stack size is passed into
// nativeInit() so mp_cstack_init_with_top() gets the real value (see
// project memory, crash-mitigation requirement #1). nativeInterrupt() and
// nativeSetDisplaySurface() are the exceptions: safe to call from any
// thread (the former just sets a pending-exception flag MicroPython's VM
// loop polls; the latter touches only display_module.cpp's own mutex-
// protected state, never MicroPython/GC state -- see its own comment
// below).

#include <jni.h>
#include <string>
#include <android/native_window_jni.h>

extern "C" {
#include "port/micropython_embed.h"
#include "py/runtime.h"
}

#include "camera_module.h"
#include "display_module.h"
#include "imu_module.h"

namespace {
// 32MB, up from an initial 4MB -- chosen after a ulab load test (see
// SESSION_STATE.yaml) found a two-array numeric workload hit MemoryError
// around 180000 elements on a 4MB heap. 32MB matches/exceeds the external
// SDRAM on most real OpenMV boards (typically 32MB), so this alone puts a
// phone in the same practical working-memory class as dedicated
// MicroPython vision hardware. This is a static array in the shared
// library's .bss section (zero-fill mmap pages, lazily committed) rather
// than JVM/ART heap, so the size itself costs nothing until actually
// touched -- no OOM risk from the declaration alone on any real device.
constexpr size_t kHeapSize = 32 * 1024 * 1024;
char g_heap[kHeapSize];
bool g_initialized = false;

// Bridges mp_embed's plain-C chunk callback (see micropython_embed.h) to
// a JNI upcall on the sink object passed into nativeExec. Lives only for
// the duration of one nativeExec() call -- see comment there.
struct ChunkCbContext {
    JNIEnv *env;
    jobject sink;
    jmethodID on_chunk_method;
};

void chunk_cb_trampoline(const char *str, size_t len, void *context) {
    auto *ctx = static_cast<ChunkCbContext *>(context);
    // str is not null-terminated at len (see micropython_embed.h) --
    // build an explicit-length std::string first, same as nativeExec
    // does below for the final accumulated output.
    std::string chunk(str, len);
    jstring jchunk = ctx->env->NewStringUTF(chunk.c_str());
    ctx->env->CallVoidMethod(ctx->sink, ctx->on_chunk_method, jchunk);
    ctx->env->DeleteLocalRef(jchunk);
}
}

extern "C" JNIEXPORT jboolean JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeInit(JNIEnv *env, jobject, jint stackSizeBytes, jstring rootPath) {
    if (g_initialized) {
        return JNI_TRUE;
    }
    const char *root_path_chars = env->GetStringUTFChars(rootPath, nullptr);
    int stack_top;
    mp_embed_init(g_heap, kHeapSize, &stack_top, static_cast<size_t>(stackSizeBytes), root_path_chars);
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

// Safe to call from any thread -- sets a pending-exception flag that the
// worker thread's VM loop polls between bytecode instructions. Does not
// touch the heap/stack/gc state, so no synchronization with the worker
// thread is needed here.
extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeInterrupt(JNIEnv *, jobject) {
    mp_sched_keyboard_interrupt();
    // Also wakes an in-flight csi.snapshot() immediately, rather than
    // leaving it to time out on its own 500ms deadline -- see
    // camera_module.cpp / SESSION_STATE.yaml's csi.snapshot() chunked-
    // wait decision. Safe from any thread, same contract as this
    // function's own.
    camera_interrupt_active_wait();
}

// Must be called on the worker thread, like nativeExec -- re-inits the
// interpreter in place (mp_deinit + mp_embed_init) using the same heap
// buffer and the same stack_size passed to the original nativeInit. This
// is the explicit user-facing Reset action (fast, deterministic,
// in-process) -- see project memory's two-tier reset design. It is NOT
// the same as the lazy idle-teardown tier (unbind -> Service destroyed ->
// state naturally gone), which needs no code here at all.
extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeReset(JNIEnv *env, jobject, jint stackSizeBytes, jstring rootPath) {
    // The ONLY teardown path where the :engine PROCESS DOES NOT DIE --
    // an open camera device is native/JNI-layer state, not MicroPython/
    // GC state, so reinitializing the interpreter below does nothing to
    // it on its own. Without this, a script interrupted mid-
    // csi.snapshot() (the one call that bypasses the worker queue) then
    // Reset would leave the camera open with nothing left to close it.
    // Idempotent, safe to call even when nothing is open -- see
    // SESSION_STATE.yaml's camera teardown hooks decision.
    camera_close_all();
    // Same reasoning as camera_close_all() just above -- an enabled
    // accelerometer/gyroscope is native/JNI-layer state, not touched by
    // mp_embed_deinit()/mp_embed_init() below.
    imu_close_all();

    if (g_initialized) {
        mp_embed_deinit();
    }
    const char *root_path_chars = env->GetStringUTFChars(rootPath, nullptr);
    int stack_top;
    mp_embed_init(g_heap, kHeapSize, &stack_top, static_cast<size_t>(stackSizeBytes), root_path_chars);
    env->ReleaseStringUTFChars(rootPath, root_path_chars);
    g_initialized = true;
}

extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeDeinit(JNIEnv *, jobject) {
    // Defense-in-depth, not a fix for a known gap (idle-unbind/crash-kill
    // both already get equivalent camera-service-side cleanup for free
    // via process death -- see SESSION_STATE.yaml) -- removes reliance
    // on that assumption rather than leaving it unverified.
    camera_close_all();
    imu_close_all();
    if (g_initialized) {
        mp_embed_deinit();
        g_initialized = false;
    }
}

// Safe from any thread -- ANativeWindow_fromSurface() needs a valid
// JNIEnv + the real jobject on the CALLING thread (a JNI local ref
// can't be queued and used later on a different thread), so the
// jobject->ANativeWindow* conversion happens synchronously right here,
// on whatever thread the call arrived on (a Binder thread pool thread in
// practice -- already attached to the JVM, so this is safe). The
// resulting plain pointer has no thread affinity, so handing it to
// display_module.cpp's own mutex-protected state is safe from here.
// Deliberately NOT routed through EngineWorker's queue (see
// EngineService.kt) -- this touches no MicroPython/GC state, only
// display_module.cpp's own lock.
extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeSetDisplaySurface(JNIEnv *env, jobject, jobject surface) {
    ANativeWindow *window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    display_set_window(window);
}
