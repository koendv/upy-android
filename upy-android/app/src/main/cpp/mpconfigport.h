/* upy-android engine port configuration.
 * Based on MicroPython's ports/embed/examples/embedding config, extended for
 * the Android engine (REPL + core stdlib, crash-mitigation requirements from
 * project memory).
 */

// Include common MicroPython embed configuration.
#include <port/mpconfigport_common.h>

// Extra-features level: f-strings, full collections, memoryview, etc.
// Flash size is not a concern on Android.
#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)
// No stdin, no readline.
#define MICROPY_PY_BUILTINS_INPUT               (0)
// vfs_posix_file.c's sys.stdout writes to fd 1, not the terminal: print() output lost.
#define MICROPY_PY_SYS_STDFILES                 (0)

// MicroPython configuration.
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_PY_GC                           (1)
#define MICROPY_PY_SYS                          (1)

// Crash-mitigation requirements. Explicit, independent of the ROM level.
#define MICROPY_STACK_CHECK                     (1)
#define MICROPY_KBD_EXCEPTION                   (1)
#define MICROPY_ENABLE_SCHEDULER                (1)

// Every real port defines its own sys.platform string; there is no default.
#define MICROPY_PY_SYS_PLATFORM                 "android"

// Floating point: unlike most flags here, this defaults to NONE
// unconditionally (py/mpconfig.h), not gated by MICROPY_CONFIG_ROM_LEVEL
// at all. This is a genuinely opt-in port decision, not a "raise the
// ROM level" one. Without it, float literals are a SyntaxError
// ("decimal numbers not supported") and the math module links but is
// largely useless.
//
// Set to FLOAT, not DOUBLE, matching every real OpenMV board.
//
// NON-OBVIOUS CROSS-REPO LINKAGE: this one MicroPython flag also
// determines ulab's NDARRAY_FLOAT byte size (v923z/micropython-ulab, a
// separate upstream repo), and therefore whether ulab arrays are
// byte-identical to LiteRT/TFLite's float32 tensors (google-ai-edge/
// LiteRT, a third separate upstream repo).
// Change this flag without knowing that,
// and litert_module.cpp's TensorBuffer read/write helpers
// silently corrupt data instead of failing loudly.
// see session-state: mpconfigport.h#MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_FLOAT)

// Real VFS, rooted at the app's own private storage (path passed in at
// runtime). See engine_jni.cpp's nativeInit()/nativeReset(), which mount
// a VfsPosix(root=rootPath) at "/" after mp_embed_init(). Scripts get a
// clean filesystem view: open("/foo.txt") or open("foo.txt") map
// transparently to real files under the app's private storage. Not a
// security boundary: os.mount(os.VfsPosix(...)) reaches whatever the
// Android app sandbox allows. Do not add port/embed_import_stub.c:
// extmod/vfs.c provides mp_import_stat/mp_builtin_open (duplicate symbols).
//
// MICROPY_PY_IO must be on for open() to actually appear as a builtin
// name. py/modbuiltins.c's builtin table gates the `open` entry on
// MICROPY_PY_IO specifically, separately from MICROPY_VFS providing the
// underlying mp_builtin_open_obj.
#define MICROPY_PY_IO                           (1)
#define MICROPY_VFS                              (1)
#define MICROPY_VFS_POSIX                        (1)
#define MICROPY_READER_POSIX                     (0)
#define MICROPY_READER_VFS                       (1)
#define MICROPY_PY_OS                            (1)
// os.uname().machine and sys.implementation._machine are
// "<board> with <mcu>".
#define MICROPY_PY_OS_UNAME                      (1)
#define MICROPY_HW_BOARD_NAME                    "upy-android"
#define MICROPY_HW_MCU_NAME                      "arm64-v8a"
// MICROPY_VFS_POSIX requires this (extmod/vfs_posix.c has a #error otherwise).
#define MICROPY_ENABLE_FINALISER                 (1)
// Defaults to (1) already (py/mpconfig.h) but made explicit rather than
// assumed, per the pattern this whole config follows. VfsPosix defaults
// to read-only otherwise (extmod/vfs_posix.c: vfs->readonly =
// !MICROPY_VFS_POSIX_WRITABLE).
#define MICROPY_VFS_POSIX_WRITABLE               (1)

// random: seed from Bionic's arc4random_buf() (mphalport.c).
unsigned long mp_android_random_seed_init(void);
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC         (mp_android_random_seed_init())

// re: match.group()/match.span() default on only at EVERYTHING level.
#define MICROPY_PY_RE_MATCH_GROUPS               (1)
#define MICROPY_PY_RE_MATCH_SPAN_START_END       (1)

// time: ports/embed provides no time HAL. Every mp_hal_ticks_*/delay_*/
// time_ns primitive is implemented in this port.
//
// MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (the generic extmod/modtime.c
// path) is deliberately left OFF: that path registers gmtime() and
// localtime() as literal aliases of the SAME function (see
// extmod/modtime.c's globals table), so it structurally cannot give
// gmtime() real UTC and localtime() real device-local time at once.
// Android has a real timezone database (Bionic's tzset()/localtime_r()),
// so MICROPY_PY_TIME_INCLUDEFILE provides real, distinct gmtime_r()/
// localtime_r()/mktime()-backed implementations via
// MICROPY_PY_TIME_EXTRA_GLOBALS instead. Same pattern ports/unix uses.
#define MICROPY_PY_TIME_TIME_TIME_NS              (1)
// MPZ is required by MICROPY_TIMESTAMP_IMPL_TIME_T below. A required
// combination, not an independent choice. Side effect: real
// arbitrary-precision Python ints everywhere (e.g. 2**100), not only for
// timestamps.
// see session-state: mpconfigport.h#MICROPY_LONGINT_IMPL_and_TIMESTAMP
#define MICROPY_LONGINT_IMPL                     (MICROPY_LONGINT_IMPL_MPZ)
// Real Unix epoch (1970), matching CPython and every libc time_t call our
// MICROPY_PY_TIME_INCLUDEFILE makes. MicroPython's own default
// (MICROPY_EPOCH_IS_2000) would silently misinterpret/misproduce
// timestamps by exactly 30 years against any real-world comparison.
#define MICROPY_EPOCH_IS_1970                    (1)
// TIME_T (signed), not the default UINT.
// see session-state: mpconfigport.h#MICROPY_LONGINT_IMPL_and_TIMESTAMP
#define MICROPY_TIMESTAMP_IMPL                   (MICROPY_TIMESTAMP_IMPL_TIME_T)
// py/mpconfig.h's `typedef time_t mp_timestamp_t;` (for the TIME_T impl
// above) references time_t directly without including <time.h> itself.
// The port is expected to provide it already in scope by this point.
#include <time.h>
// NOT under port/. MICROPY_PY_TIME_INCLUDEFILE is textually #include'd
// into extmod/modtime.c, never compiled as its own translation unit, so
// it must live somewhere no glob (py/*.c, port/*.c, shared/runtime/*.c,
// extmod/*.c) picks up as a separate TU, or it gets double-compiled (same
// bug class hit with extmod/lib/re1.5/*.c). Resolves via -Imicropython_embed
// to the bundle's own root, which no existing glob touches.
#define MICROPY_PY_TIME_INCLUDEFILE              "modtime_android.c"

// deflate compression: off at every level below FULL_FEATURES.
// binascii.crc32 is the standard CRC-32 (zlib/PNG), unlike OpenMV's crc.crc32().
#define MICROPY_PY_DEFLATE_COMPRESS              (1)

// Frozen Python modules (native-bringup/manifest.py), e.g. asyncio.
#define MICROPY_MODULE_FROZEN_MPY                (1)
#define MICROPY_QSTR_EXTRA_POOL                  mp_qstr_frozen_const_pool

// see session-state: mpconfigport.h#MICROPY_PY_CRC
