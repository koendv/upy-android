/* upy-android engine port configuration.
 * Based on MicroPython's ports/embed/examples/embedding config, extended for
 * the Android engine (REPL + core stdlib, crash-mitigation requirements from
 * project memory).
 */

// Include common MicroPython embed configuration.
#include <port/mpconfigport_common.h>

// Core-features level: compiler + reasonable stdlib coverage for a REPL,
// not just the bare minimum the embedding example uses.
#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

// MicroPython configuration.
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_PY_GC                           (1)
#define MICROPY_PY_SYS                          (1)

// Crash-mitigation requirements (see project memory, "Crash-mitigation
// requirements"). These are #ifndef-guarded in py/mpconfig.h and default to
// on only at ROM level EXTRA_FEATURES (30); CORE_FEATURES is 10, so they
// must be forced on explicitly here rather than assumed from ROM level.
#define MICROPY_STACK_CHECK                     (1)
#define MICROPY_KBD_EXCEPTION                   (1)
#define MICROPY_ENABLE_SCHEDULER                (1)

// Every real port defines its own sys.platform string; there is no default.
#define MICROPY_PY_SYS_PLATFORM                 "android"

// Floating point: unlike most flags here, this defaults to NONE
// unconditionally (py/mpconfig.h) -- not gated by MICROPY_CONFIG_ROM_LEVEL
// at all, a genuinely opt-in port decision, not a "raise the ROM level"
// one. Without it, float literals are a SyntaxError ("decimal numbers not
// supported") and the math module links but is largely useless. DOUBLE
// (not FLOAT) since this is a full 64-bit Android target with no memory
// constraint forcing single-precision -- matches CPython's own semantics.
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_DOUBLE)

// Real VFS, rooted at the app's own private storage (path passed in at
// runtime -- see engine_jni.cpp's nativeInit()/nativeReset(), which mount
// a VfsPosix(root=rootPath) at "/" after mp_embed_init()). Scripts get a
// clean, jailed filesystem view: open("/foo.txt") or open("foo.txt") map
// transparently to real files under the app's private storage, nothing
// outside it is reachable. Superseded the original "no VFS at all"
// decision once a concrete need (scripts doing their own file I/O)
// showed up -- see project memory. port/embed_import_stub.c is REMOVED
// (not just disabled) since extmod/vfs.c now provides the real
// mp_import_stat/mp_builtin_open; leaving the stub in would be a
// duplicate-symbol link error.
//
// MICROPY_PY_IO must be on for open() to actually appear as a builtin
// name -- py/modbuiltins.c's builtin table gates the `open` entry on
// MICROPY_PY_IO specifically, separately from MICROPY_VFS providing the
// underlying mp_builtin_open_obj.
#define MICROPY_PY_IO                           (1)
#define MICROPY_VFS                              (1)
#define MICROPY_VFS_POSIX                        (1)
#define MICROPY_READER_POSIX                     (0)
#define MICROPY_READER_VFS                       (1)
#define MICROPY_PY_OS                            (1)
// MICROPY_VFS_POSIX requires this explicitly (extmod/vfs_posix.c has a
// #error otherwise) -- defaults on only at EXTRA_FEATURES ROM level.
#define MICROPY_ENABLE_FINALISER                 (1)
// Defaults to (1) already (py/mpconfig.h) but made explicit rather than
// assumed, per the pattern this whole config follows -- VfsPosix defaults
// to read-only otherwise (extmod/vfs_posix.c: vfs->readonly =
// !MICROPY_VFS_POSIX_WRITABLE).
#define MICROPY_VFS_POSIX_WRITABLE               (1)

// random module: like most flags here, off by default at CORE_FEATURES
// (needs EXTRA_FEATURES). EXTRA_FUNCS pulls in randrange/randint/choice/
// random/uniform, not just getrandbits/seed. The PRNG itself (Yasmarang,
// in extmod/modrandom.c) runs entirely in C with no OS involvement per
// call -- only the initial seed needs real entropy, via this hook.
// mp_android_random_seed_init() (mphalport.c) wraps Bionic's
// arc4random_buf() -- no JNI/Kotlin round-trip, no fd/permission
// handling, available since long before our minSdk 26.
#define MICROPY_PY_RANDOM                        (1)
#define MICROPY_PY_RANDOM_EXTRA_FUNCS            (1)
unsigned long mp_android_random_seed_init(void);
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC         (mp_android_random_seed_init())

// json module: off by default at CORE_FEATURES. No extra port support
// needed -- modjson.c only touches py/objstringio.h + py/stream.h, both
// already present (MICROPY_PY_IO is already on for VFS above).
#define MICROPY_PY_JSON                          (1)

// re module: off by default at CORE_FEATURES. MATCH_GROUPS/
// SPAN_START_END default even higher (EVERYTHING, not just
// EXTRA_FEATURES) but are turned on explicitly here since match.group()/
// match.span() are basic, expected regex usage, not a rarely-needed
// extra. RE_DEBUG stays off (dumpcode.c not vendored, not needed).
#define MICROPY_PY_RE                            (1)
#define MICROPY_PY_RE_SUB                        (1)
#define MICROPY_PY_RE_MATCH_GROUPS               (1)
#define MICROPY_PY_RE_MATCH_SPAN_START_END       (1)

// time module: off by default at CORE_FEATURES (needs BASIC_FEATURES,
// numerically 20 vs our 10 -- higher number is MORE features, easy to
// misread). ports/embed provides NO time HAL at all (unlike math/json/
// random/re, which needed nothing beyond a config flag + vendoring) --
// every mp_hal_ticks_*/delay_*/time_ns primitive below is ours.
//
// MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (the generic extmod/modtime.c
// path) is deliberately left OFF: that path registers gmtime() and
// localtime() as literal aliases of the SAME function (see
// extmod/modtime.c's globals table), so it structurally cannot give
// gmtime() real UTC and localtime() real device-local time at once --
// most embedded ports using it just fake localtime()==UTC because they
// have no OS timezone database anyway. Android has a real one (Bionic's
// tzset()/localtime_r(), timezone/DST-aware), so we use it: our
// MICROPY_PY_TIME_INCLUDEFILE provides real, distinct gmtime_r()/
// localtime_r()/mktime()-backed implementations via
// MICROPY_PY_TIME_EXTRA_GLOBALS instead -- same pattern ports/unix uses,
// minus its custom interruptible-select sleep (we already get an
// interruptible, non-busy-waiting sleep for free from the generic
// time_sleep() -> mp_hal_delay_ms(), see mphalport.c).
#define MICROPY_PY_TIME                          (1)
#define MICROPY_PY_TIME_TIME_TIME_NS              (1)
// Required by MICROPY_TIMESTAMP_IMPL_TIME_T below, discovered the hard
// way (OverflowError: small int overflow on time.mktime(), even for an
// ordinary small value like a few hours' timezone offset from epoch):
// shared/timeutils/timeutils.h's timeutils_obj_from_timestamp(), for the
// TIME_T impl, unconditionally calls mp_obj_new_int_from_ll() rather than
// MP_OBJ_NEW_SMALL_INT() directly -- and py/objint.c's version of that
// function (used when MICROPY_LONGINT_IMPL is NONE, our previous default)
// is a hard-coded "small int overflow" raise with NO magnitude check at
// all, regardless of whether the value would actually fit. ports/unix
// pairs MICROPY_TIMESTAMP_IMPL_TIME_T with MICROPY_LONGINT_IMPL_MPZ for
// exactly this reason -- not a coincidence, a required combination. No
// new vendoring needed: py/mpz.c and py/objint_mpz.c are already in the
// bundle (embed.mk copies all of py/*.c unconditionally regardless of
// config). Side effect, not just a time-module implementation detail:
// this enables real arbitrary-precision Python ints everywhere in the
// interpreter (e.g. 2**100), not only for timestamps.
#define MICROPY_LONGINT_IMPL                     (MICROPY_LONGINT_IMPL_MPZ)
// Real Unix epoch (1970), matching CPython and every libc time_t call our
// MICROPY_PY_TIME_INCLUDEFILE makes -- MicroPython's own default
// (MICROPY_EPOCH_IS_2000) would silently misinterpret/misproduce
// timestamps by exactly 30 years against any real-world comparison.
#define MICROPY_EPOCH_IS_1970                    (1)
// mp_timestamp_t must be a real (signed) time_t, matching every libc call
// our MICROPY_PY_TIME_INCLUDEFILE makes -- the default
// MICROPY_TIMESTAMP_IMPL_UINT is UNSIGNED, and mktime()'s local-time ->
// UTC conversion can transiently produce a small NEGATIVE offset
// (whenever the device's timezone differs from UTC), which then wraps
// around to a huge unsigned value and overflows small-int representation
// on the way back into an mp_obj_t. ports/unix hits this exact
// requirement for the same reason (see its own "for time_t, needed by
// MICROPY_TIMESTAMP_IMPL_TIME_T" comment) -- same fix here.
#define MICROPY_TIMESTAMP_IMPL                   (MICROPY_TIMESTAMP_IMPL_TIME_T)
// py/mpconfig.h's `typedef time_t mp_timestamp_t;` (for the TIME_T impl
// above) references time_t directly without including <time.h> itself --
// the port is expected to provide it already in scope by this point.
#include <time.h>
// NOT under port/ -- MICROPY_PY_TIME_INCLUDEFILE is textually #include'd
// into extmod/modtime.c, never compiled as its own translation unit, so
// it must live somewhere no glob (py/*.c, port/*.c, shared/runtime/*.c,
// extmod/*.c) picks up as a separate TU, or it gets double-compiled (same
// bug class hit with extmod/lib/re1.5/*.c). Resolves via -Imicropython_embed
// to the bundle's own root, which no existing glob touches.
#define MICROPY_PY_TIME_INCLUDEFILE              "modtime_android.c"

// binascii module: off by default at CORE_FEATURES (needs EXTRA_FEATURES).
// hexlify/unhexlify additionally need MICROPY_PY_BUILTINS_BYTES_HEX (also
// EXTRA_FEATURES-gated) -- the underlying mp_obj_bytes_hex/bytes_fromhex
// implementations are core py/objstr.c, already unconditionally compiled,
// this flag just exposes them (and bytes.hex()/bytes.fromhex() as a side
// effect, which is a reasonable pairing, not scope creep). crc32 is
// deliberately left off (MICROPY_PY_BINASCII_CRC32 stays at its
// EXTRA_FEATURES-gated default of off) -- enabling it would pull in
// lib/uzlib/uzlib.h, a whole extra vendoring dependency, for one function
// most scripts won't need; hexlify/unhexlify/a2b_base64/b2a_base64 cover
// the common case with zero extra files beyond modbinascii.c itself.
#define MICROPY_PY_BINASCII                      (1)
#define MICROPY_PY_BUILTINS_BYTES_HEX            (1)

// heapq module: off by default at CORE_FEATURES (needs EXTRA_FEATURES).
// extmod/modheapq.c has zero further dependencies (no vendored lib,
// unlike binascii's crc32/hashlib/deflate) -- a genuinely free add,
// same tier as errno/cmath below.
#define MICROPY_PY_HEAPQ                         (1)

// uctypes module: off by default at CORE_FEATURES (needs
// EXTRA_FEATURES). extmod/moductypes.c has zero further dependencies,
// same "genuinely free" tier as heapq above.
#define MICROPY_PY_UCTYPES                       (1)

// select module: off by default at CORE_FEATURES (needs
// EXTRA_FEATURES). extmod/modselect.c uses real POSIX <poll.h>
// directly -- no vendored lib. MICROPY_PY_SELECT_SELECT (the classic
// select.select() call, not just poll()-based objects) defaults to the
// same tier as the parent flag and is turned on explicitly here for
// consistency with re's MATCH_GROUPS/SPAN_START_END above (basic,
// expected usage, not a rarely-needed extra).
// MICROPY_PY_SELECT_POSIX_OPTIMISATIONS is left at its explicit
// off-by-default (regardless of rom level) -- an internal
// implementation-detail optimization, not investigated, no evidence
// it's needed.
#define MICROPY_PY_SELECT                        (1)
#define MICROPY_PY_SELECT_SELECT                 (1)

// cmath module: off by default at CORE_FEATURES (needs EXTRA_FEATURES),
// but py/modcmath.c is core (unconditionally compiled, like py/moderrno.c)
// and MICROPY_PY_BUILTINS_COMPLEX already defaults to on (it just mirrors
// MICROPY_PY_BUILTINS_FLOAT, already 1 for the math module) -- genuinely
// free, one flag, zero vendoring, same tier as errno.
#define MICROPY_PY_CMATH                         (1)

// errno module: off by default at CORE_FEATURES (needs EXTRA_FEATURES),
// but py/moderrno.c is core (unconditionally compiled) -- genuinely
// free, one flag, zero vendoring, same tier as cmath. Pairs naturally
// with the VFS work: OSError codes currently print as bare numbers
// (e.g. OSError(2,)), this lets scripts use errno.ENOENT etc. by name.
#define MICROPY_PY_ERRNO                         (1)

// help() / help('modules'): off by default at CORE_FEATURES (needs
// EXTRA_FEATURES), but py/builtinhelp.c is core (unconditionally
// compiled by embed.mk's py/*.c glob) -- genuinely free, two flags,
// zero vendoring, same tier as errno/cmath. help('modules') enumerates
// every MP_REGISTER_MODULE'd module (walks genhdr's module registry),
// so it picks up ulab and everything else added this session
// automatically -- no separate list to maintain.
#define MICROPY_PY_BUILTINS_HELP                 (1)
#define MICROPY_PY_BUILTINS_HELP_MODULES         (1)
