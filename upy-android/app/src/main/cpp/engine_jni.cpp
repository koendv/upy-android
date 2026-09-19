// JNI bridge for the upy-android MicroPython engine, running inside the
// :engine process (see EngineService.kt / AndroidManifest.xml).
// see session-state: engine_jni.cpp#threading_contract

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
#include "tf_module.h"

namespace {
// see session-state: engine_jni.cpp#kHeapSize
constexpr size_t kHeapSize = 32 * 1024 * 1024;
char g_heap[kHeapSize];
bool g_initialized = false;

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

// Must be called on the worker thread, like nativeExec.
// see session-state: engine_jni.cpp#nativeReset
extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeReset(JNIEnv *env, jobject, jint stackSizeBytes, jstring rootPath) {
    camera_close_all();
    imu_close_all();
    tf_close_all();

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
    // Defense-in-depth, not a fix for a known gap. Idle-unbind/
    // crash-kill both already get equivalent camera-service-side
    // cleanup for free via process death; this removes reliance on
    // that assumption rather than leaving it unverified.
    camera_close_all();
    imu_close_all();
    tf_close_all();
    if (g_initialized) {
        mp_embed_deinit();
        g_initialized = false;
    }
}

// see session-state: engine_jni.cpp#nativeSetDisplaySurface
extern "C" JNIEXPORT void JNICALL
Java_eu_kdvelectronics_upyandroid_Engine_nativeSetDisplaySurface(JNIEnv *env, jobject, jobject surface) {
    ANativeWindow *window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    display_set_window(window);
}
